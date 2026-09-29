#include "channel_service.h"
#include "app_state.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <cstring>
#include <cstdio>

namespace {
struct Command { uint32_t node; int type; meshtastic_Channel channel; meshtastic_ChannelSet set; bool replace, apply_lora; };
struct Reply {
    int kind; uint32_t request; int error;
    meshtastic_Channel channel;
    meshtastic_Config_LoRaConfig lora;
    meshtastic_AdminMessage_session_passkey_t key;
};
SemaphoreHandle_t mutex;
QueueHandle_t commands, replies;
channel_send_fn send_frame;
channel_snapshot_t state{};
uint32_t waiting_id = 0, local_node = 0;
uint32_t operation_node = 0;
meshtastic_AdminMessage_session_passkey_t session_key{};
void status(const char* text) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(state.status, sizeof(state.status), "%s", text);
    ++state.generation;
    xSemaphoreGive(mutex);
}
void store_channel(const meshtastic_Channel& c) {
    if (c.index < 0 || c.index >= 8) return;
    state.channels[c.index] = c; state.known[c.index] = true; ++state.generation;
}
uint32_t packet_id() { uint32_t id; do { id = esp_random(); } while (!id); return id; }
bool transmit(const meshtastic_AdminMessage& admin, uint32_t id, bool response) {
    app_snapshot_t app; app_state_snapshot(&app);
    if (app.state != CONN_READY || !app.my_num || app.my_num != operation_node) { status("Radio desconectado ou sincronizando."); return false; }
    meshtastic_ToRadio radio = meshtastic_ToRadio_init_zero;
    radio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    auto& p = radio.packet;
    p.to = app.my_num; p.id = id; p.channel = 0;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    p.decoded.want_response = response;
    auto payload = pb_ostream_from_buffer(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes));
    if (!pb_encode(&payload, meshtastic_AdminMessage_fields, &admin)) { status("Configuracao excede o pacote do radio."); return false; }
    p.decoded.payload.size = payload.bytes_written;
    uint8_t frame[512]; auto stream = pb_ostream_from_buffer(frame, sizeof(frame));
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &radio) || send_frame(frame, stream.bytes_written) != ESP_OK) {
        status("Falha ao enviar ao radio."); return false;
    }
    return true;
}
bool get(int index, Reply& reply) { // -1 = LoRa config
    for (int attempt = 0; attempt < 3; ++attempt) {
        meshtastic_AdminMessage request = meshtastic_AdminMessage_init_zero;
        if (index < 0) {
            request.which_payload_variant = meshtastic_AdminMessage_get_config_request_tag;
            request.get_config_request = meshtastic_AdminMessage_ConfigType_LORA_CONFIG;
        } else {
            request.which_payload_variant = meshtastic_AdminMessage_get_channel_request_tag;
            request.get_channel_request = index + 1;
        }
        uint32_t id = packet_id();
        xQueueReset(replies);
        xSemaphoreTake(mutex, portMAX_DELAY); waiting_id = id; xSemaphoreGive(mutex);
        if (!transmit(request, id, true)) return false;
        if (xQueueReceive(replies, &reply, pdMS_TO_TICKS(3500)) == pdTRUE && reply.request == id) {
            if (reply.kind == 3) {
                char text[100]; snprintf(text, sizeof(text), "Radio recusou administracao (codigo %d).", reply.error); status(text); return false;
            }
            if ((index < 0 && reply.kind == 2) || (index >= 0 && reply.kind == 1 && reply.channel.index == index)) {
                session_key = reply.key; return true;
            }
        }
    }
    status("Sem resposta administrativa. Confira conexao/permissoes do radio."); return false;
}
bool same_channel(const meshtastic_Channel& a, const meshtastic_Channel& b) {
    if (a.index != b.index || a.role != b.role) return false;
    if (a.role == meshtastic_Channel_Role_DISABLED) return true;
    return b.has_settings && !strcmp(a.settings.name, b.settings.name) &&
        a.settings.psk.size == b.settings.psk.size &&
        !memcmp(a.settings.psk.bytes, b.settings.psk.bytes, a.settings.psk.size) &&
        a.settings.uplink_enabled == b.settings.uplink_enabled && a.settings.downlink_enabled == b.settings.downlink_enabled;
}
bool write_channel(const meshtastic_Channel& c) {
    Reply reply{};
    if (!get(c.index, reply)) return false; // fresh admin session key
    meshtastic_AdminMessage request = meshtastic_AdminMessage_init_zero;
    request.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    request.set_channel = c; request.session_passkey = session_key;
    if (!transmit(request, packet_id(), false)) return false;
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!get(c.index, reply)) return false;
    if (!same_channel(c, reply.channel)) { status("Radio nao confirmou a alteracao do canal. Releia os canais."); return false; }
    return true;
}
bool refresh() {
    Reply reply{};
    for (int i = 0; i < 8; ++i) if (!get(i, reply)) return false;
    return get(-1, reply);
}
void worker(void*) {
    Command cmd{};
    while (true) {
        xQueueReceive(commands, &cmd, portMAX_DELAY);
        operation_node = cmd.node;
        status("Lendo configuracao do radio...");
        bool ok = refresh();
        channel_snapshot_t snap{}; channel_service_snapshot(&snap);
        if (ok && cmd.type == 1) {
            const auto& c = cmd.channel;
            // Preserve the one primary: editor only changes existing role,
            // creates secondary in disabled slots, or disables a secondary.
            auto current = snap.channels[c.index].role;
            if ((current == meshtastic_Channel_Role_PRIMARY && c.role != current) ||
                (current != meshtastic_Channel_Role_PRIMARY && c.role == meshtastic_Channel_Role_PRIMARY)) {
                status("Use importar/substituir para trocar o canal primario."); ok = false;
            } else { status("Gravando canal e conferindo no radio..."); ok = write_channel(c); }
        }
        if (ok && cmd.type == 2) {
            meshtastic_Channel plan[8]{}; int count = 0;
            if (cmd.replace) {
                for (unsigned i = 0; i < 8; ++i) {
                    auto& c = plan[count++]; c.index = i;
                    if (i < cmd.set.settings_count) {
                        c.has_settings = true; c.settings = cmd.set.settings[i];
                        c.role = i == 0 ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY;
                    }
                }
            } else {
                int free_slots[8], free_count = 0;
                for (int i = 1; i < 8; ++i) if (snap.channels[i].role == meshtastic_Channel_Role_DISABLED) free_slots[free_count++] = i;
                for (unsigned i = 0; i < cmd.set.settings_count; ++i) {
                    bool duplicate = false;
                    for (int j = 0; j < 8; ++j) {
                        const auto& existing = snap.channels[j]; const auto& proposed = cmd.set.settings[i];
                        if (existing.role != meshtastic_Channel_Role_DISABLED && !strcmp(existing.settings.name, proposed.name) &&
                            existing.settings.psk.size == proposed.psk.size && !memcmp(existing.settings.psk.bytes, proposed.psk.bytes, proposed.psk.size)) duplicate = true;
                    }
                    if (duplicate) continue;
                    if (count >= free_count) { status("Sem espaco para todos os canais; nada foi alterado."); ok = false; break; }
                    auto& c = plan[count]; c.index = free_slots[count++]; c.has_settings = true;
                    c.settings = cmd.set.settings[i];
                    channel_settings_resolve_inherited_key(&c.settings, cmd.set.settings[0]);
                    c.role = meshtastic_Channel_Role_SECONDARY;
                }
            }
            if (ok) {
                status("Importando: gravando e conferindo cada canal...");
                for (int i = 0; i < count && ok; ++i) ok = write_channel(plan[i]);
            }
            if (ok && cmd.replace && cmd.apply_lora && cmd.set.has_lora_config) {
                Reply reply{}; ok = get(-1, reply);
                if (ok) {
                    // A share link describes modulation, not regional authorization,
                    // transmit power, MQTT policy or local hardware configuration.
                    auto config = reply.lora; const auto& imported = cmd.set.lora_config;
                    config.use_preset = imported.use_preset; config.modem_preset = imported.modem_preset;
                    config.bandwidth = imported.bandwidth; config.spread_factor = imported.spread_factor;
                    config.coding_rate = imported.coding_rate; config.frequency_offset = imported.frequency_offset;
                    config.channel_num = imported.channel_num; config.override_frequency = imported.override_frequency;
                    meshtastic_AdminMessage request = meshtastic_AdminMessage_init_zero;
                    request.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
                    request.set_config.which_payload_variant = meshtastic_Config_lora_tag;
                    request.set_config.payload_variant.lora = config; request.session_passkey = session_key;
                    ok = transmit(request, packet_id(), false);
                    if (ok) {
                        status("Canais confirmados. Aguardando reinicio para verificar LoRa...");
                        vTaskDelay(pdMS_TO_TICKS(4000));
                        app_snapshot_t app{};
                        for (unsigned wait = 0; wait < 30; ++wait) {
                            app_state_snapshot(&app);
                            if (app.state == CONN_READY && app.my_num == operation_node) break;
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                        ok = get(-1, reply);
                        if (ok) {
                            const auto& actual = reply.lora;
                            ok = actual.use_preset == config.use_preset && actual.modem_preset == config.modem_preset &&
                                 actual.channel_num == config.channel_num && actual.override_frequency == config.override_frequency &&
                                 actual.frequency_offset == config.frequency_offset && actual.region == config.region &&
                                 (config.use_preset || (actual.bandwidth == config.bandwidth && actual.spread_factor == config.spread_factor && actual.coding_rate == config.coding_rate));
                            status(ok ? "Canais e LoRa confirmados pelo radio." : "Canais gravados, mas LoRa diverge. Releia antes de tentar novamente.");
                        } else status("Canais gravados; LoRa ainda nao confirmado. Reconecte e releia do radio.");
                    }
                }
            } else if (ok) status(cmd.type == 2 ? "Importacao confirmada pelo radio." : "Configuracao confirmada pelo radio.");
        } else if (ok) status("Configuracao confirmada pelo radio.");
        // On partial failure, confirmed channels already reflect the actual radio;
        // never show an optimistic all-or-nothing success.
        xSemaphoreTake(mutex, portMAX_DELAY);
        state.busy = false; waiting_id = 0; ++state.generation;
        xSemaphoreGive(mutex);
        memset(&cmd, 0, sizeof(cmd)); memset(&session_key, 0, sizeof(session_key));
    }
}
bool enqueue(Command cmd) {
    app_snapshot_t app; app_state_snapshot(&app);
    if (app.state != CONN_READY || !app.my_num) { if (mutex) status("Conecte o radio e aguarde a sincronizacao."); return false; }
    cmd.node = app.my_num;
    if (!mutex || !commands) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (state.busy) { xSemaphoreGive(mutex); return false; }
    state.busy = true;
    snprintf(state.status, sizeof(state.status), "Operacao aguardando radio..."); ++state.generation;
    xSemaphoreGive(mutex);
    if (xQueueSend(commands, &cmd, 0) == pdTRUE) return true;
    xSemaphoreTake(mutex, portMAX_DELAY); state.busy = false; xSemaphoreGive(mutex); return false;
}
}
esp_err_t channel_service_init(channel_send_fn send) {
    send_frame = send; mutex = xSemaphoreCreateMutex();
    commands = xQueueCreate(1, sizeof(Command)); replies = xQueueCreate(4, sizeof(Reply));
    if (!mutex || !commands || !replies) return ESP_ERR_NO_MEM;
    status("Aguardando canais do radio.");
    return xTaskCreate(worker, "channel_admin", 16384, nullptr, 3, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void channel_service_snapshot(channel_snapshot_t* out) {
    if (!mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(mutex, portMAX_DELAY); *out = state; xSemaphoreGive(mutex);
}
void channel_service_on_frame(const uint8_t* data, size_t size) {
    if (!mutex) return;
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    auto stream = pb_istream_from_buffer(data, size);
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &fr)) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (fr.which_payload_variant == meshtastic_FromRadio_my_info_tag) {
        local_node = fr.my_info.my_node_num;
        memset(state.known, 0, sizeof(state.known)); state.has_lora = false; ++state.generation;
    } else if (fr.which_payload_variant == meshtastic_FromRadio_channel_tag) store_channel(fr.channel);
    else if (fr.which_payload_variant == meshtastic_FromRadio_config_tag && fr.config.which_payload_variant == meshtastic_Config_lora_tag) {
        state.lora = fr.config.payload_variant.lora; state.has_lora = true; ++state.generation;
    } else if (fr.which_payload_variant == meshtastic_FromRadio_packet_tag && fr.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        const auto& p = fr.packet; const auto& d = p.decoded;
        if (waiting_id && d.request_id == waiting_id && (p.from == local_node || p.from == 0)) {
            Reply reply{}; reply.request = d.request_id;
            auto ps = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
            if (d.portnum == meshtastic_PortNum_ADMIN_APP) {
                // Fixed-size nanopb structures; no allocation in receive callbacks.
                meshtastic_AdminMessage decoded_admin{};
                auto admin = &decoded_admin;
                if (pb_decode(&ps, meshtastic_AdminMessage_fields, admin)) {
                    reply.key = admin->session_passkey;
                    if (admin->which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag) {
                        reply.kind = 1; reply.channel = admin->get_channel_response; store_channel(reply.channel);
                    } else if (admin->which_payload_variant == meshtastic_AdminMessage_get_config_response_tag &&
                               admin->get_config_response.which_payload_variant == meshtastic_Config_lora_tag) {
                        reply.kind = 2; reply.lora = admin->get_config_response.payload_variant.lora;
                        state.lora = reply.lora; state.has_lora = true; ++state.generation;
                    }
                }

            } else if (d.portnum == meshtastic_PortNum_ROUTING_APP) {
                meshtastic_Routing routing = meshtastic_Routing_init_zero;
                if (pb_decode(&ps, meshtastic_Routing_fields, &routing) && routing.which_variant == meshtastic_Routing_error_reason_tag && routing.error_reason != meshtastic_Routing_Error_NONE) {
                    reply.kind = 3; reply.error = routing.error_reason;
                }
            }
            if (reply.kind) xQueueSend(replies, &reply, 0);
        }
    }
    xSemaphoreGive(mutex);
}
bool channel_service_save(const meshtastic_Channel& c) {
    if (c.index < 0 || c.index >= 8 || c.role > meshtastic_Channel_Role_SECONDARY || !channel_settings_valid(c.settings)) return false;
    Command cmd{}; cmd.type = 1; cmd.channel = c; return enqueue(cmd);
}
bool channel_service_import(const meshtastic_ChannelSet& set, bool replace, bool apply_lora) {
    if (!set.settings_count || set.settings_count > 8) return false;
    for (unsigned i = 0; i < set.settings_count; ++i) if (!channel_settings_valid(set.settings[i])) return false;
    Command cmd{}; cmd.type = 2; cmd.set = set; cmd.replace = replace; cmd.apply_lora = apply_lora; return enqueue(cmd);
}
bool channel_service_refresh() { Command cmd{}; return enqueue(cmd); }
bool channel_service_share(int index, char* url, size_t cap) {
    channel_snapshot_t snap; channel_service_snapshot(&snap);
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
    if (index >= 0) {
        if (index >= 8 || !snap.known[index] || snap.channels[index].role == meshtastic_Channel_Role_DISABLED) return false;
        set.settings[set.settings_count++] = snap.channels[index].settings;
        if (snap.channels[index].role == meshtastic_Channel_Role_SECONDARY && !set.settings[0].psk.size) {
            bool resolved = false;
            for (int i = 0; i < 8; ++i) if (snap.known[i] && snap.channels[i].role == meshtastic_Channel_Role_PRIMARY) {
                channel_settings_resolve_inherited_key(&set.settings[0], snap.channels[i].settings); resolved = true; break;
            }
            if (!resolved) return false;
        }
    } else {
        for (int role = 1; role <= 2; ++role) for (int i = 0; i < 8; ++i)
            if (snap.known[i] && snap.channels[i].role == role) set.settings[set.settings_count++] = snap.channels[i].settings;
    }
    if (index < 0 && (set.settings_count == 0 || !snap.has_lora)) return false;
    set.has_lora_config = snap.has_lora; set.lora_config = snap.lora;
    return channel_url_encode(set, index >= 0 && snap.channels[index].role != meshtastic_Channel_Role_PRIMARY, url, cap);
}
