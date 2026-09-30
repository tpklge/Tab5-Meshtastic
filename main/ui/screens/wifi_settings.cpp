#include "wifi_settings.h"
#include "wifi_service.h"
#include "../theme.h"
#include <cstring>
#include <cstdio>

namespace {
lv_obj_t *network_list, *status_label, *ip_label, *time_label, *scan_button, *saved_button;
lv_obj_t *modal, *ssid_input, *password_input, *keyboard, *input_status, *focused;
wifi_snapshot_t snapshot{};
uint32_t generation = UINT32_MAX;

lv_obj_t* label(lv_obj_t* parent, const char* text) {
    lv_obj_t* obj = lv_label_create(parent); lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(C_HI), 0);
    return obj;
}
lv_obj_t* row(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0); lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 12, 0); lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback) {
    lv_obj_t* obj = lv_button_create(parent);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_pad_hor(obj, 16, 0);
    lv_obj_center(label(obj, text));
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, nullptr);
    return obj;
}
void close() {
    if (password_input) lv_textarea_set_text(password_input, "");
    if (modal) lv_obj_delete(modal);
    modal = ssid_input = password_input = keyboard = input_status = focused = nullptr;
}
void connect() {
    if (!ssid_input || !password_input) return;
    const char* ssid = lv_textarea_get_text(ssid_input);
    const char* password = lv_textarea_get_text(password_input);
    if (!wifi_service_connect(ssid, password, false)) {
        lv_label_set_text(input_status, "Confira o nome (1-32 bytes) e a senha (8-63 caracteres ou 64 hex; vazia para rede aberta).");
        return;
    }
    close();
    lv_label_set_text(status_label, "Conexao solicitada...");
}
void focus(lv_event_t* event) {
    focused = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_keyboard_set_textarea(keyboard, focused);
}
void new_modal(const char* title) {
    close();
    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modal, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 20, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(modal, 12, 0);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    label(modal, title);
}
void configure(lv_event_t*) {
    new_modal("Conectar ao Wi-Fi (2.4 GHz)");
    lv_obj_t* fields = row(modal);
    auto input = [&](const char* placeholder, int max) {
        lv_obj_t* obj = lv_textarea_create(fields);
        lv_obj_set_size(obj, 570, 52); lv_textarea_set_one_line(obj, true);
        lv_textarea_set_max_length(obj, max); lv_textarea_set_placeholder_text(obj, placeholder);
        lv_obj_add_event_cb(obj, focus, LV_EVENT_CLICKED, nullptr);
        return obj;
    };
    ssid_input = input("Nome da rede (SSID)", 32);
    password_input = input("Senha (vazia para rede aberta)", 64);
    lv_textarea_set_password_mode(password_input, true);
    uint32_t selected = lv_dropdown_get_selected(network_list);
    if (selected < snapshot.count) lv_textarea_set_text(ssid_input, snapshot.networks[selected].ssid);
    else if (snapshot.saved_ssid[0]) lv_textarea_set_text(ssid_input, snapshot.saved_ssid);
    lv_obj_t* actions = row(modal);
    button(actions, "Conectar e salvar", [](lv_event_t*) { connect(); });
    button(actions, "Mostrar/ocultar senha", [](lv_event_t*) {
        lv_textarea_set_password_mode(password_input, !lv_textarea_get_password_mode(password_input));
    });
    button(actions, "Cancelar", [](lv_event_t*) { close(); });
    input_status = label(modal, "A rede sera salva apos conectar. Redes ocultas: digite o nome completo.");
    lv_obj_set_width(input_status, lv_pct(100)); lv_label_set_long_mode(input_status, LV_LABEL_LONG_MODE_WRAP);
    keyboard = lv_keyboard_create(modal);
    lv_obj_set_size(keyboard, lv_pct(100), 360);
    focused = lv_textarea_get_text(ssid_input)[0] ? password_input : ssid_input;
    lv_keyboard_set_textarea(keyboard, focused);
    lv_obj_add_event_cb(keyboard, [](lv_event_t*) { connect(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, [](lv_event_t*) { close(); }, LV_EVENT_CANCEL, nullptr);
}
void queued(bool ok) { lv_label_set_text(status_label, ok ? "Solicitacao enviada..." : "Aguarde a operacao anterior e tente novamente."); }
}
void wifi_settings_make(lv_obj_t* parent) {
    label(parent, "WI-FI");
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_SURF2), 0);
    lv_obj_set_style_border_width(card, 0, 0); lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0); lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    status_label = label(card, "Wi-Fi desligado.");
    ip_label = label(card, "");
    lv_obj_t* networks = row(card);
    network_list = lv_dropdown_create(networks); lv_obj_set_width(network_list, 470);
    lv_dropdown_set_options(network_list, "Busque redes ou digite o nome");
    scan_button = button(networks, "Buscar redes", [](lv_event_t*) { queued(wifi_service_scan()); });
    button(networks, "Configurar rede", configure);
    lv_obj_t* actions = row(card);
    saved_button = button(actions, "Conectar rede salva", [](lv_event_t*) { queued(wifi_service_connect(nullptr, nullptr, true)); });
    button(actions, "Desligar Wi-Fi", [](lv_event_t*) { queued(wifi_service_disconnect(false)); });
    button(actions, "Esquecer rede", [](lv_event_t*) {
        new_modal("Esquecer a rede Wi-Fi salva?");
        label(modal, "A senha sera removida e a reconexao automatica sera desativada.");
        lv_obj_t* actions = row(modal);
        button(actions, "Esquecer", [](lv_event_t*) { queued(wifi_service_disconnect(true)); close(); });
        button(actions, "Cancelar", [](lv_event_t*) { close(); });
    });
    time_label = label(card, "");
    button(card, "Sincronizar hora pela internet", [](lv_event_t*) { queued(wifi_service_sync_time()); });
    wifi_settings_refresh();
}
void wifi_settings_refresh() {
    if (!status_label) return;
    // UI callbacks are serialized by the LVGL lock, including initial construction.
    // Keep large scratch buffers off the small app_main stack used during boot.
    static wifi_snapshot_t next; wifi_service_snapshot(&next);
    if (next.generation == generation) return;
    generation = next.generation;
    bool networks_changed = next.count != snapshot.count || memcmp(next.networks, snapshot.networks, sizeof(next.networks));
    snapshot = next;
    lv_label_set_text(status_label, snapshot.status); lv_label_set_text(time_label, snapshot.time_status);
    char info[128];
    if (snapshot.connected) snprintf(info, sizeof(info), "Rede: %s | IP: %s", snapshot.ssid, snapshot.ip);
    else snprintf(info, sizeof(info), "Rede salva: %s", snapshot.saved ? snapshot.saved_ssid : "nenhuma");
    lv_label_set_text(ip_label, info);
    if (snapshot.busy) lv_obj_add_state(scan_button, LV_STATE_DISABLED); else lv_obj_remove_state(scan_button, LV_STATE_DISABLED);
    if (snapshot.saved && !snapshot.busy) lv_obj_remove_state(saved_button, LV_STATE_DISABLED); else lv_obj_add_state(saved_button, LV_STATE_DISABLED);
    if (networks_changed) {
        static char options[1600];
        options[0] = 0;
        for (int i = 0; i < snapshot.count; ++i) {
            char name[33]; strlcpy(name, snapshot.networks[i].ssid, sizeof(name));
            for (char* p = name; *p; ++p) if ((unsigned char)*p < 32) *p = ' ';
            char option[80]; snprintf(option, sizeof(option), "%s%s | %d dBm%s", i ? "\n" : "", name, snapshot.networks[i].rssi, snapshot.networks[i].secured ? " | senha" : " | aberta");
            strlcat(options, option, sizeof(options));
        }
        lv_dropdown_set_options(network_list, snapshot.count ? options : "Nenhuma rede encontrada; informe o nome");
    }
}
void wifi_settings_leave() { close(); }
void wifi_settings_key(const char* text, bool backspace, bool submit) {
    if (!focused) return;
    if (backspace) lv_textarea_delete_char(focused);
    else if (submit) connect();
    else if (text) lv_textarea_add_text(focused, text);
}
