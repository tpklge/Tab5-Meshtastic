#include "wifi_service.h"
#include "app_clock.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>
#include <cstdio>

namespace {
enum Action { SCAN, CONNECT, DISCONNECT, FORGET, SYNC };
struct Command { Action action; char ssid[33]; char password[65]; bool use_saved; };
struct Credentials { char ssid[33]; char password[65]; };
SemaphoreHandle_t mutex;
QueueHandle_t queue;
TaskHandle_t task;
wifi_snapshot_t state{};
Credentials saved{}, pending{};
nvs_handle_t storage;
esp_netif_t* netif;
bool driver_initialized = false, handlers_registered = false, started = false, net_initialized = false;
bool desired = false, sntp = false, auto_connect = false;
unsigned retries = 0;
int64_t retry_at = 0, connect_deadline = 0, sync_deadline = 0;
std::atomic<bool> got_ip{false}, lost_ip{false};
std::atomic<unsigned> disconnect_reason{0};
void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mutex); }
void status(const char* text, bool busy = false) {
    lock(); strlcpy(state.status, text, sizeof(state.status)); state.busy = busy; ++state.generation; unlock();
}
void time_status(const char* text) {
    lock(); strlcpy(state.time_status, text, sizeof(state.time_status)); ++state.generation; unlock();
}
void failed(const char* operation, esp_err_t err) {
    char text[128]; snprintf(text, sizeof(text), "%s: %s", operation, esp_err_to_name(err)); status(text);
}
void event(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) got_ip = true;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        disconnect_reason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
        lost_ip = true;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) lost_ip = true;
}
esp_err_t ensure_driver() {
    if (started) return ESP_OK;
    status("Iniciando Wi-Fi...", true);
    esp_err_t err;
    if (!net_initialized) {
        err = esp_netif_init(); if (err != ESP_OK) return err;
        err = esp_event_loop_create_default(); if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        net_initialized = true;
    }
    if (!netif) { netif = esp_netif_create_default_wifi_sta(); if (!netif) return ESP_ERR_NO_MEM; }
    if (!handlers_registered) {
        err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event, nullptr); if (err != ESP_OK) return err;
        err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event, nullptr); if (err != ESP_OK) return err;
        handlers_registered = true;
    }
    if (!driver_initialized) {
        err = esp_hosted_init(); if (err != ESP_OK) return err;
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&config); if (err != ESP_OK) return err;
        driver_initialized = true;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) return err;
    err = esp_wifi_start(); if (err != ESP_OK) return err;
    started = true;
    return ESP_OK;
}
void stop_sync() { if (sntp) { esp_netif_sntp_deinit(); sntp = false; } sync_deadline = 0; }
void begin_sync() {
    stop_sync();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) { time_status("Falha ao iniciar sincronizacao de hora."); return; }
    sntp = true;
    sync_deadline = esp_timer_get_time() + 20000000;
    time_status("Consultando hora na internet...");
}
void connect_now() {
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) { desired = false; failed("Falha ao conectar", err); return; }
    connect_deadline = esp_timer_get_time() + 25000000;
    status("Conectando ao Wi-Fi...", true);
}
void handle(const Command& cmd) {
    if (cmd.action == DISCONNECT || cmd.action == FORGET) {
        desired = false; retry_at = connect_deadline = 0;
        stop_sync();
        esp_err_t err = ESP_OK;
        if (started) { esp_wifi_disconnect(); err = esp_wifi_stop(); if (err == ESP_OK) started = false; }
        if (storage) {
            esp_err_t saved_err = nvs_set_u8(storage, "auto", 0);
            if (cmd.action == FORGET) {
                esp_err_t erase = nvs_erase_key(storage, "credentials");
                if (erase != ESP_OK && erase != ESP_ERR_NVS_NOT_FOUND) saved_err = erase;
            }
            if (saved_err == ESP_OK) saved_err = nvs_commit(storage);
            if (saved_err != ESP_OK) err = saved_err;
        }
        if (cmd.action == FORGET && err == ESP_OK) memset(&saved, 0, sizeof(saved));
        memset(&pending, 0, sizeof(pending));
        lock(); state.connected = false; state.ip[0] = 0; state.ssid[0] = 0;
        state.saved = saved.ssid[0]; strlcpy(state.saved_ssid, saved.ssid, sizeof(state.saved_ssid)); unlock();
        got_ip = false; lost_ip = false;
        if (err != ESP_OK) failed("Falha ao desligar/salvar", err);
        else status(cmd.action == FORGET ? "Rede esquecida. Wi-Fi desligado." : "Wi-Fi desligado. Rede mantida para reconectar.");
        time_status("Sincronizacao pela internet parada.");
        return;
    }
    if (cmd.action == SYNC) {
        lock(); bool connected = state.connected; unlock();
        if (connected) begin_sync(); else time_status("Conecte o Wi-Fi para sincronizar.");
        return;
    }
    if (cmd.action == SCAN && connect_deadline) { status("Aguarde a conexao antes de buscar redes.", true); return; }
    esp_err_t err = ensure_driver();
    if (err != ESP_OK) { failed("Wi-Fi indisponivel", err); return; }
    if (cmd.action == SCAN) {
        status("Buscando redes 2.4 GHz...", true);
        wifi_scan_config_t config{}; config.show_hidden = true;
        err = esp_wifi_scan_start(&config, true);
        if (err != ESP_OK) { failed("Falha na busca", err); return; }
        wifi_ap_record_t aps[20]{}; uint16_t count = 20;
        err = esp_wifi_scan_get_ap_records(&count, aps);
        if (err != ESP_OK) { esp_wifi_clear_ap_list(); failed("Falha ao ler redes", err); return; }
        lock(); state.count = 0;
        for (int i = 0; i < count; ++i) {
            if (!aps[i].ssid[0]) continue;
            bool duplicate = false;
            for (int j = 0; j < state.count; ++j) if (!strcmp(state.networks[j].ssid, (char*)aps[i].ssid)) duplicate = true;
            if (duplicate) continue;
            auto& network = state.networks[state.count++];
            strlcpy(network.ssid, (char*)aps[i].ssid, sizeof(network.ssid));
            network.rssi = aps[i].rssi; network.secured = aps[i].authmode != WIFI_AUTH_OPEN;
        }
        unlock();
        status("Busca concluida. Escolha uma rede ou informe o nome.");
        return;
    }
    if (cmd.action == CONNECT) {
        if (cmd.use_saved) {
            if (!saved.ssid[0]) { status("Nenhuma rede salva."); return; }
            pending = saved;
        } else {
            strlcpy(pending.ssid, cmd.ssid, sizeof(pending.ssid));
            strlcpy(pending.password, cmd.password, sizeof(pending.password));
        }
        desired = false; retry_at = connect_deadline = 0; stop_sync();
        lock(); state.connected = false; state.ip[0] = 0; unlock();
        esp_wifi_disconnect();
        // Let the old connection's disconnect event drain before the next attempt.
        vTaskDelay(pdMS_TO_TICKS(250)); got_ip = false; lost_ip = false;
        wifi_config_t config{};
        memcpy(config.sta.ssid, pending.ssid, strlen(pending.ssid));
        memcpy(config.sta.password, pending.password, strlen(pending.password));
        config.sta.threshold.authmode = pending.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        config.sta.pmf_cfg.capable = true;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&config, 0, sizeof(config));
        if (err != ESP_OK) { failed("Falha na configuracao", err); return; }
        lock(); state.connected = false; state.ip[0] = 0; strlcpy(state.ssid, pending.ssid, sizeof(state.ssid)); unlock();
        desired = true; retries = 0; retry_at = 0;
        connect_now();
    }
}
void worker(void*) {
    if (auto_connect && saved.ssid[0]) { Command cmd{}; cmd.action = CONNECT; cmd.use_saved = true; handle(cmd); }
    for (;;) {
        Command cmd{};
        if (xQueueReceive(queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) { handle(cmd); memset(&cmd, 0, sizeof(cmd)); }
        int64_t now = esp_timer_get_time();
        if (lost_ip.exchange(false) && desired) {
            lock(); state.connected = false; state.ip[0] = 0; unlock();
            stop_sync(); time_status("Wi-Fi desconectado; hora local mantida.");
            connect_deadline = 0;
            if (++retries <= 3) { retry_at = now + 3000000; status("Conexao perdida. Tentando novamente...", true); }
            else { desired = false; char message[128]; snprintf(message, sizeof(message), "Falha na conexao (motivo %u). Confira rede/senha e tente novamente.", disconnect_reason.load()); status(message); }
        }
        if (got_ip.exchange(false) && desired) {
            wifi_ap_record_t ap{}; esp_netif_ip_info_t ip{};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && !strcmp((char*)ap.ssid, pending.ssid) && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
                retries = 0; retry_at = connect_deadline = 0;
                saved = pending;
                esp_err_t err = storage ? nvs_set_blob(storage, "credentials", &saved, sizeof(saved)) : ESP_ERR_INVALID_STATE;
                if (err == ESP_OK) err = nvs_set_u8(storage, "auto", 1);
                if (err == ESP_OK) err = nvs_commit(storage);
                lock(); state.connected = true; state.saved = err == ESP_OK;
                strlcpy(state.saved_ssid, saved.ssid, sizeof(state.saved_ssid));
                snprintf(state.ip, sizeof(state.ip), IPSTR, IP2STR(&ip.ip)); unlock();
                status(err == ESP_OK ? "Wi-Fi conectado. Rede salva." : "Wi-Fi conectado; falha ao salvar a rede.");
                begin_sync();
            }
        }
        if (desired && retry_at && now >= retry_at) { retry_at = 0; connect_now(); }
        if (desired && connect_deadline && now >= connect_deadline) {
            desired = false; connect_deadline = 0; esp_wifi_disconnect(); status("Tempo de conexao esgotado. Confira rede/senha e tente novamente.");
        }
        if (sntp && sync_deadline) {
            if (esp_netif_sntp_sync_wait(0) == ESP_OK) {
                esp_err_t err = app_clock_sync(time(nullptr));
                time_status(err == ESP_OK ? "Hora sincronizada e salva no RTC." : "Hora da internet recebida; falha ao salvar no RTC.");
                stop_sync();
            } else if (now >= sync_deadline) {
                stop_sync(); time_status("Sem resposta NTP. Confira o acesso a internet e tente novamente.");
            }
        }
    }
}
bool submit(const Command& cmd) { return queue && xQueueSend(queue, &cmd, 0) == pdTRUE; }
}
esp_err_t wifi_service_init() {
    mutex = xSemaphoreCreateMutex(); queue = xQueueCreate(4, sizeof(Command));
    if (!mutex || !queue) return ESP_ERR_NO_MEM;
    strlcpy(state.status, "Wi-Fi desligado. Toque em Buscar redes.", sizeof(state.status));
    strlcpy(state.time_status, "Conecte o Wi-Fi para obter a hora da internet.", sizeof(state.time_status));
    if (nvs_open("t5wifi", NVS_READWRITE, &storage) == ESP_OK) {
        size_t size = sizeof(saved);
        if (nvs_get_blob(storage, "credentials", &saved, &size) != ESP_OK || size != sizeof(saved) || !memchr(saved.ssid, 0, sizeof(saved.ssid)) || !memchr(saved.password, 0, sizeof(saved.password))) memset(&saved, 0, sizeof(saved));
        uint8_t value = 0; nvs_get_u8(storage, "auto", &value); auto_connect = value == 1;
        state.saved = saved.ssid[0]; strlcpy(state.saved_ssid, saved.ssid, sizeof(state.saved_ssid));
    }
    return ESP_OK;
}
esp_err_t wifi_service_start() { return xTaskCreate(worker, "wifi_service", 8192, nullptr, 3, &task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM; }
void wifi_service_snapshot(wifi_snapshot_t* out) { lock(); *out = state; unlock(); }
bool wifi_service_scan() { Command cmd{}; cmd.action = SCAN; return submit(cmd); }
bool wifi_service_connect(const char* ssid, const char* password, bool use_saved) {
    Command cmd{}; cmd.action = CONNECT; cmd.use_saved = use_saved;
    if (!use_saved) {
        if (!ssid || !password || !*ssid || strlen(ssid) > 32 || strlen(password) > 64 || (*password && strlen(password) < 8)) return false;
        if (strlen(password) == 64) for (const char* p = password; *p; ++p) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
        strlcpy(cmd.ssid, ssid, sizeof(cmd.ssid)); strlcpy(cmd.password, password, sizeof(cmd.password));
    }
    bool ok = submit(cmd); memset(cmd.password, 0, sizeof(cmd.password)); return ok;
}
bool wifi_service_disconnect(bool forget) { Command cmd{}; cmd.action = forget ? FORGET : DISCONNECT; return submit(cmd); }
bool wifi_service_sync_time() { Command cmd{}; cmd.action = SYNC; return submit(cmd); }
