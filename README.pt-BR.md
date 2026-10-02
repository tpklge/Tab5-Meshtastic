# Tab5-Meshtastic v2

Cliente Meshtastic para o **M5Stack Tab5** (ESP32-P4 + ESP32-C6 via esp-hosted/SDIO).

Derivado de [hardparking/Tab5-Meshtastic](https://github.com/hardparking/Tab5-Meshtastic).
Mantido como repositório privado — o upstream não possui licença de distribuição.

---

## Funcionalidades

| Área | Descrição |
|------|-----------|
| **Transporte BLE** | Descoberta por UUID, pareamento com PIN, dispositivo salvo, reconexão automática |
| **Transporte UART** | RAK3172H via porta Grove (GPIO53/54, 115200 baud); seleção persiste entre reinicializações |
| **Chat** | Bolhas de mensagem por canal, histórico persistente (SPIFFS), rascunhos por canal |
| **Nós** | Lista ordenada, barras de sinal, última visão, bateria, telemetria |
| **Canais** | 8 slots, editor completo (nome, PSK, flags MQTT), geração de QR, importação via câmera |
| **Wi-Fi** | Scan assíncrono, até 4 redes salvas, reconexão automática |
| **Relógio** | Sincronização SNTP pós-IP; fallback para RTC manual; carimbos no chat |
| **Bateria** | Leitura INA226 via I2C, tensão exibida na faixa de diagnóstico |
| **Brilho** | Controle via LEDC (5–100%), persiste entre boots |
| **Áudio** | Codec ES8388, padrões de bipe configuráveis, volume persistido |
| **Câmera** | Captura CSI para importação de QR de canal |
| **Launcher** | Compatível com M5Launcher 2.8 (suspend limpo, partição OTA ≤ 2 MB) |
| **Diagnóstico** | Faixa de status; pressão longa abre overlay completo |

---

## Hardware necessário

- **M5Stack Tab5** (ESP32-P4 + ESP32-C6)
- **Bateria deve estar conectada** antes de ligar ou gravar — USB sozinho não sustenta a placa
- Rádio Meshtastic externo: BLE (ex: M5Stack Unit C6L) **ou** RAK3172H via Grove

---

## Build e gravação

```bash
# Configurar ambiente (primeira vez ou após mudanças no sdkconfig)
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4

# Compilar
idf.py build

# Gravar (bateria obrigatória)
idf.py -p /dev/ttyACM0 flash monitor
```

> **Regra:** sempre faça `git commit` antes de gravar. O único teste real é um boot frio
> com bateria — serial reseta o P4, então qualquer mudança ruim precisa ser reversível via
> `git revert`.

### Testes headless (sem hardware)

```bash
cd tests && cmake -B build && cmake --build build && ./build/tab5_tests
```

---

## Arquitetura

```
main/
  app/        — AppState (fonte única de verdade, protegida por mutex), clock
  ble/        — Transporte BLE (NimBLE central via esp-hosted)
  transport/  — Interface IMeshTransport + UartTransport (RAK3172H)
  protocol/   — MeshSession (handshake Meshtastic), channel_url (QR)
  mesh/       — mesh_proto: encode/decode nanopb puro (sem BLE, sem LVGL)
  ui/         — Shell LVGL, telas, theme.h (tokens de design)
  board/      — lcd_tools, teclado, battery_monitor, áudio, câmera
  storage/    — settings_store, message_store (SPIFFS), app_storage
  network/    — wifi_service, SNTP
```

**Regra de ouro:** BLE/transporte nunca toca LVGL; UI nunca chama transporte exceto via
`app_commands.h`.

### Modelo de threads

- **Tarefa BLE/transporte** → publica em `AppState` via `app_state_*()`.
- **Tarefa LVGL** → lê snapshot imutável via `app_state_snapshot()`; emite comandos
  apenas por `app_send_text()` / `ble_transport_*()`.

---

## Restrições de hardware

| Restrição | Detalhe |
|-----------|---------|
| BLE é poll, não notificação | Notificações FromNum não atravessam o túnel esp-hosted; poll de ~600 ms |
| Callbacks de leitura podem não disparar | Timeout de 1,5 s obrigatório em toda leitura FromRadio |
| Display via PPA | 1280×720 rotação por hardware — NÃO usar rotação LVGL em runtime |
| MTU | Solicitar 512 (negocia 255); aguardar MTU antes da primeira leitura |
| Serial reseta o P4 | Diagnósticos na tela são a superfície de observabilidade principal |
| Boot frio ≠ reset USB | Mudanças no caminho de boot devem ser validadas com bateria real |

---

## Documentação adicional

| Arquivo | Conteúdo |
|---------|----------|
| `PRD.md` | Especificação completa: requisitos, design, contrato GATT |
| `CHANGELOG.md` | Histórico detalhado de funcionalidades adicionadas |
| `UPSTREAM.md` | Status de licença e base do fork |
| `docs/RAK3172_GROVE.md` | Pinagem Grove, cabeamento, protocolo de framing 0x94C3 |
| `docs/CHANNELS_QR.md` | Formato QR, encoding proto, fluxo de importação pela câmera |
| `docs/CHAT_CLOCK.md` | Regras de timestamp e fonte de tempo |
| `docs/STORAGE_SCHEMA.md` | Formatos binários versionados com CRC32 |
| `docs/TEST_MATRIX.md` | Casos de teste automatizados e de hardware |
| `docs/LAUNCHER.md` | Instalação OTA, backup/restore, layout de partições |
| `docs/LAUNCHER_INTEROP.md` | Integração com M5Launcher 2.8 (teclado e Wi-Fi) |
