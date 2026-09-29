#include "channels_screen.h"
#include "channel_service.h"
#include "channel_camera.h"
#include "settings_store.h"
#include "app_state.h"
#include "../theme.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <cstdio>
#include <cstring>

namespace {
lv_obj_t *panel, *rows, *state_label, *modal, *body, *notice, *keyboard, *focused;
lv_obj_t *name_input, *key_input, *role_select, *uplink, *downlink, *url_input, *mode_select, *lora_check;
lv_obj_t *preview_canvas;
uint16_t* preview_pixels;
uint32_t preview_gen = 0, last_gen = UINT32_MAX;
meshtastic_Channel editing{};
meshtastic_ChannelSet imported{};
channel_snapshot_t snapshot{};
bool camera_open = false;
char shared_url[CHANNEL_URL_MAX];
lv_obj_t* label(lv_obj_t* parent, const char* text) {
    auto l = lv_label_create(parent); lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(C_HI), 0);
    lv_obj_set_style_text_font(l, FONT_BODY, 0); return l;
}
lv_obj_t* row(lv_obj_t* parent) {
    auto r = lv_obj_create(parent); lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_SURF), 0); lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 8, 0); lv_obj_set_style_pad_column(r, 12, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); return r;
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, intptr_t data = 0) {
    auto b = lv_button_create(parent); lv_obj_set_height(b, 48);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_GREEN), 0);
    auto l = label(b, text); lv_obj_set_style_text_color(l, lv_color_hex(C_INK), 0); lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(data)); return b;
}
void tell(const char* text) { if (notice) lv_label_set_text(notice, text); }
void close_modal(lv_event_t* = nullptr) {
    channel_camera_stop(); camera_open = false; preview_canvas = nullptr;
    if (modal) lv_obj_delete(modal);
    modal = body = notice = keyboard = focused = nullptr;
    name_input = key_input = role_select = uplink = downlink = nullptr;
    url_input = mode_select = lora_check = nullptr;
    memset(&editing, 0, sizeof(editing)); memset(&imported, 0, sizeof(imported));
    memset(shared_url, 0, sizeof(shared_url));
}
void new_modal(const char* title) {
    close_modal();
    modal = lv_obj_create(panel); lv_obj_add_flag(modal, LV_OBJ_FLAG_FLOATING); lv_obj_set_size(modal, lv_pct(100), lv_pct(100)); lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(C_BG), 0); lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    auto head = row(modal); auto t = label(head, title); lv_obj_set_flex_grow(t, 1);
    button(head, "Fechar", close_modal);
    notice = label(modal, ""); lv_obj_set_width(notice, lv_pct(100)); lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
    body = lv_obj_create(modal); lv_obj_set_width(body, lv_pct(100)); lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_color(body, lv_color_hex(C_BG), 0); lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    keyboard = lv_keyboard_create(modal); lv_obj_set_height(keyboard, 210); lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL)
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, nullptr);
}
lv_obj_t* input(lv_obj_t* parent, const char* text, uint32_t max) {
    auto t = lv_textarea_create(parent); lv_obj_set_width(t, lv_pct(100)); lv_textarea_set_one_line(t, true);
    lv_textarea_set_max_length(t, max); lv_textarea_set_text(t, text);
    lv_obj_add_event_cb(t, [](lv_event_t* e) {
        focused = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_keyboard_set_textarea(keyboard, focused); lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr); return t;
}
void fingerprint(const meshtastic_ChannelSettings& s, char* out, size_t cap) {
    uint8_t hash[32]; mbedtls_sha256(s.psk.bytes, s.psk.size, hash, 0);
    snprintf(out, cap, "%s / %02X%02X%02X%02X", s.psk.size == 32 ? "AES-256" : s.psk.size == 16 ? "AES-128" : s.psk.size == 1 ? "Chave curta" : "Sem chave",
             hash[0], hash[1], hash[2], hash[3]);
}
void generate_key(lv_event_t*) {
    uint8_t key[32];
    // P4 has no local RF entropy source. Its SAR noise source is otherwise off
    // after boot; this application does not use the ADC for measurements.
    bootloader_random_enable();
    esp_fill_random(key, sizeof(key));
    bootloader_random_disable();
    unsigned char b64[48]; size_t n;
    if (mbedtls_base64_encode(b64, sizeof(b64), &n, key, sizeof(key)) == 0) lv_textarea_set_text(key_input, reinterpret_cast<char*>(b64));
    memset(key, 0, sizeof(key)); memset(b64, 0, sizeof(b64));
    tell("Nova chave AES-256 gerada. Salve para aplica-la ao radio.");
}
void save_channel(lv_event_t*) {
    auto c = editing;
    const char* name = lv_textarea_get_text(name_input);
    if (strlen(name) >= sizeof(c.settings.name)) { tell("Nome deve ter no maximo 11 bytes UTF-8."); return; }
    snprintf(c.settings.name, sizeof(c.settings.name), "%s", name);
    const char* key = lv_textarea_get_text(key_input); size_t n = 0;
    memset(c.settings.psk.bytes, 0, sizeof(c.settings.psk.bytes));
    if (*key && mbedtls_base64_decode(c.settings.psk.bytes, sizeof(c.settings.psk.bytes), &n, reinterpret_cast<const unsigned char*>(key), strlen(key)) != 0) {
        tell("Chave Base64 invalida."); return;
    }
    c.settings.psk.size = n;
    if (!channel_settings_valid(c.settings)) { tell("Chave deve conter 0, 1, 16 ou 32 bytes; confira tambem o nome."); return; }
    if (editing.role != meshtastic_Channel_Role_PRIMARY)
        c.role = lv_dropdown_get_selected(role_select) == 0 ? meshtastic_Channel_Role_SECONDARY : meshtastic_Channel_Role_DISABLED;
    c.has_settings = c.role != meshtastic_Channel_Role_DISABLED;
    c.settings.uplink_enabled = lv_obj_has_state(uplink, LV_STATE_CHECKED);
    c.settings.downlink_enabled = lv_obj_has_state(downlink, LV_STATE_CHECKED);
    if (!channel_service_save(c)) { tell("Nao foi possivel iniciar. Aguarde a operacao atual."); return; }
    if (c.role == meshtastic_Channel_Role_DISABLED && settings_store_get()->sel_channel == c.index) settings_store_set_channel(0);
    close_modal();
}
void edit_channel(lv_event_t* e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    channel_service_snapshot(&snapshot);
    if (snapshot.busy || !snapshot.known[index]) return;
    auto c = snapshot.channels[index];
    new_modal("Editar canal"); editing = c;
    label(body, "Nome (ate 11 bytes UTF-8)"); name_input = input(body, c.settings.name, 11);
    label(body, "Chave Base64 (vazia: primario sem chave / secundario herda a primaria)");
    unsigned char b64[48]{}; size_t n;
    mbedtls_base64_encode(b64, sizeof(b64), &n, c.settings.psk.bytes, c.settings.psk.size);
    key_input = input(body, reinterpret_cast<char*>(b64), 44); lv_textarea_set_password_mode(key_input, true);
    auto actions = row(body);
    button(actions, "Gerar AES-256", generate_key);
    button(actions, "Mostrar/ocultar chave", [](lv_event_t*) { lv_textarea_set_password_mode(key_input, !lv_textarea_get_password_mode(key_input)); });
    if (c.role == meshtastic_Channel_Role_PRIMARY) label(body, "Papel: primario (define a frequencia quando automatica)");
    else {
        role_select = lv_dropdown_create(body); lv_dropdown_set_options(role_select, "Secundario\nDesativado");
        lv_dropdown_set_selected(role_select, 0);
    }
    uplink = lv_checkbox_create(body); lv_checkbox_set_text(uplink, "Permitir uplink MQTT");
    downlink = lv_checkbox_create(body); lv_checkbox_set_text(downlink, "Permitir downlink MQTT");
    if (c.settings.uplink_enabled) lv_obj_add_state(uplink, LV_STATE_CHECKED);
    if (c.settings.downlink_enabled) lv_obj_add_state(downlink, LV_STATE_CHECKED);
    button(body, "Salvar no radio", save_channel);
    focused = name_input;
    if (c.role == meshtastic_Channel_Role_DISABLED) generate_key(nullptr);
}
void share(lv_event_t* e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    char url[CHANNEL_URL_MAX];
    if (!channel_service_share(index, url, sizeof(url))) return;
    new_modal("Compartilhar canais"); snprintf(shared_url, sizeof(shared_url), "%s", url);
    tell("O QR inclui as chaves dos canais. Compartilhe somente com quem deve participar.");
    auto r = row(body);
    auto qr = lv_qrcode_create(r); lv_qrcode_set_size(qr, 420); lv_qrcode_set_quiet_zone(qr, true);
    lv_qrcode_set_dark_color(qr, lv_color_black()); lv_qrcode_set_light_color(qr, lv_color_white());
    if (lv_qrcode_update(qr, shared_url, strlen(shared_url)) != LV_RESULT_OK) tell("Link grande demais para o QR. Compartilhe um canal por vez.");
    auto text = label(r, shared_url); lv_obj_set_width(text, 550); lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
}
void use_channel(lv_event_t* e) {
    unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    settings_store_set_channel(i); last_gen = UINT32_MAX;
}
void confirm_import(lv_event_t*) {
    bool replace = lv_dropdown_get_selected(mode_select) == 1;
    bool apply_lora = lv_obj_has_state(lora_check, LV_STATE_CHECKED);
    if (!channel_service_import(imported, replace, replace && apply_lora)) { tell("Aguarde a operacao atual."); return; }
    if (replace) settings_store_set_channel(0);
    close_modal();
}
void review(const char* url) {
    meshtastic_ChannelSet candidate{}; bool add;
    if (!channel_url_decode(url, &candidate, &add)) { tell("Link de canais Meshtastic invalido ou nao suportado."); return; }
    new_modal("Revisar importacao"); imported = candidate;
    tell("Adicionar preserva os canais e a modulacao atuais. Substituir remove os canais ausentes no QR.");
    for (unsigned i = 0; i < candidate.settings_count; ++i) {
        char fp[80], text[160]; fingerprint(candidate.settings[i], fp, sizeof(fp));
        snprintf(text, sizeof(text), "%u. %s — %s", i + 1, candidate.settings[i].name[0] ? candidate.settings[i].name : "Canal padrao", fp); label(body, text);
    }
    mode_select = lv_dropdown_create(body); lv_obj_set_width(mode_select, 550);
    lv_dropdown_set_options(mode_select, "Adicionar como secundarios\nSubstituir conjunto (primeiro = primario)");
    lv_dropdown_set_selected(mode_select, add ? 0 : 1);
    lora_check = lv_checkbox_create(body); lv_checkbox_set_text(lora_check, "Ao substituir, aplicar modulacao/frequencia do QR");
    if (candidate.has_lora_config) {
        lv_obj_add_state(lora_check, LV_STATE_CHECKED);
        char text[170]; const auto& l = candidate.lora_config;
        snprintf(text, sizeof(text), "LoRa: preset %u, canal RF %lu, frequencia %.3f MHz. Regiao e potencia locais preservadas.",
                 (unsigned)l.modem_preset, (unsigned long)l.channel_num, (double)l.override_frequency); label(body, text);
    } else lv_obj_add_state(lora_check, LV_STATE_DISABLED);
    button(body, "Confirmar importacao no radio", confirm_import);
}
void import_link(lv_event_t*) {
    new_modal("Importar link Meshtastic");
    tell("Cole ou digite o link https://meshtastic.org/e/#... para revisar.");
    url_input = input(body, "", CHANNEL_URL_MAX - 1); focused = url_input;
    button(body, "Revisar canais", [](lv_event_t*) {
        char url[CHANNEL_URL_MAX]; snprintf(url, sizeof(url), "%s", lv_textarea_get_text(url_input)); review(url);
    });
}
void scan(lv_event_t*) {
    new_modal("Ler QR pela camera"); camera_open = true;
    if (!preview_pixels) preview_pixels = static_cast<uint16_t*>(heap_caps_calloc(QR_PREVIEW_W * QR_PREVIEW_H, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!preview_pixels) { tell("Memoria insuficiente para visualizacao."); camera_open = false; return; }
    preview_canvas = lv_canvas_create(body);
    lv_canvas_set_buffer(preview_canvas, preview_pixels, QR_PREVIEW_W, QR_PREVIEW_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_style_transform_scale(preview_canvas, 384, 0);
    esp_err_t err = channel_camera_start();
    if (err != ESP_OK) { tell("Camera ocupada ou indisponivel. Aguarde e tente novamente."); camera_open = false; }
}
}
lv_obj_t* channels_screen_make(lv_obj_t* parent) {
    panel = lv_obj_create(parent); lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(panel, lv_color_hex(C_BG), 0); lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    auto actions = row(panel);
    button(actions, "Reler do radio", [](lv_event_t*) { channel_service_refresh(); });
    button(actions, "QR do conjunto", share, -1);
    button(actions, "Ler QR", scan);
    button(actions, "Importar link", import_link);
    state_label = label(panel, "Aguardando canais do radio..."); lv_obj_set_width(state_label, lv_pct(100));
    rows = lv_obj_create(panel); lv_obj_set_width(rows, lv_pct(100)); lv_obj_set_flex_grow(rows, 1);
    lv_obj_set_style_bg_color(rows, lv_color_hex(C_BG), 0); lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    return panel;
}
void channels_screen_refresh() {
    if (!panel) return;
    if (camera_open) {
        char url[CHANNEL_URL_MAX]{}, text[120]{};
        bool found = channel_camera_poll(url, sizeof(url), text, sizeof(text), preview_pixels, &preview_gen);
        tell(text); if (preview_canvas) lv_obj_invalidate(preview_canvas);
        if (found) { camera_open = false; review(url); }
    }
    channel_service_snapshot(&snapshot);
    if (last_gen == snapshot.generation) return;
    last_gen = snapshot.generation;
    lv_label_set_text(state_label, snapshot.status);
    lv_obj_clean(rows);
    for (int i = 0; i < 8; ++i) {
        const auto& c = snapshot.channels[i];
        auto r = row(rows); char text[150], fp[80]; fingerprint(c.settings, fp, sizeof(fp));
        snprintf(text, sizeof(text), "%d  %s  |  %s%s", i,
                 !snapshot.known[i] ? "Nao lido" : c.role == meshtastic_Channel_Role_DISABLED ? "Livre" : c.settings.name[0] ? c.settings.name : "Padrao",
                 c.role == meshtastic_Channel_Role_PRIMARY ? "Primario" : c.role == meshtastic_Channel_Role_SECONDARY ? "Secundario" : "Desativado",
                 settings_store_get()->sel_channel == i ? "  [CHAT]" : "");
        auto l = label(r, text); lv_obj_set_width(l, 420);
        auto b = button(r, c.role == meshtastic_Channel_Role_DISABLED ? "Criar" : "Editar", edit_channel, i);
        if (!snapshot.known[i] || snapshot.busy) lv_obj_add_state(b, LV_STATE_DISABLED);
        if (snapshot.known[i] && c.role != meshtastic_Channel_Role_DISABLED) {
            button(r, "QR", share, i); button(r, "Usar no chat", use_channel, i);
            auto f = label(r, fp); lv_obj_set_style_text_font(f, FONT_META, 0);
        }
    }
}
void channels_screen_leave() { close_modal(); }
void channels_screen_key(const char* text, bool backspace, bool enter) {
    if (!focused || !modal) return;
    if (backspace) lv_textarea_delete_char(focused);
    else if (enter) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    else if (text) lv_textarea_add_text(focused, text);
}
