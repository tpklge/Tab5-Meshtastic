# Compatibilidade com o M5Launcher no Tab5

Analise do codigo publico `bmorcelli/Launcher` em `cd392f28` (branch `main`,
2026-10-01). O comportamento do firmware instalado pode diferir conforme a versao.

## Teclado A164

O Launcher inicia primeiro uma busca por CardKB2 no Grove (GPIO53/54,
endereco 0x5F). Se nao encontrar, inicializa o teclado A164 na porta interna
GPIO0/1, endereco 0x6D, interrupcao GPIO50. Usa o modo **Normal** e habilita
somente a interrupcao desse modo (`INT_CFG=0x01`).

Nossa aplicacao usa o modo **Character/String** para receber texto pronto.
O controlador do teclado conserva `INT_CFG` quando muda de modo. O driver local
mudava para Character sem habilitar a interrupcao correspondente; se o Launcher
houvesse deixado `INT_CFG=0x01`, a fila Character nao seria lida. Agora, ao
iniciar, habilitamos `INT_CFG=0x04`; ao sair, paramos o polling, restauramos
Normal com `INT_CFG=0x01`, limpamos fila e status de interrupcao e liberamos o
I2C. O desligamento pelo botao SAIR corta a alimentacao em vez de reiniciar.

A busca de CardKB2 no Grove pode ser afetada pelo RAK ligado aos mesmos pinos
GPIO53/54. Isso e uma hipotese para a lentidao anterior do Launcher, nao uma
causa confirmada da falha do A164. Comparar inicializacao do Launcher com e sem
RAK ligado, sempre com o Tab5 desligado antes de mudar o cabo.

## Wi-Fi do ESP32-C6

O Launcher liga o C6 pelo expansor de I2C durante `M5.begin()` e inicia
ESP-Hosted por SDIO nos GPIO8-13, com reset GPIO15. A inicializacao do Wi-Fi
usa `wifi_init_config_t.nvs_enable=false` e `WIFI_STORAGE_RAM`. Nossa aplicacao
agora usa os mesmos ajustes, evitando salvar configuracao de Wi-Fi no flash.

O Launcher guarda o estado da tentativa ESP-Hosted na NVS compartilhada:
namespace `launcher`, chave `hosted_st`. O valor 1 indica tentativa em andamento;
3 indica falha bloqueada. Nesse estado, ele nao tenta inicializar o Wi-Fi
novamente ate `CFG > Retry WiFi Module` ou `wifi hosted retry` na consola serial.
Ao usar SAIR, nossa aplicacao desliga o C6 e, se encontrar 1 ou 3, retorna a
chave a 0 para permitir uma nova tentativa no proximo boot. Se houver uma
incompatibilidade real de firmware ou ligacao, o Launcher voltara a detectar a
falha; esse ajuste nao substitui o diagnostico do C6.

Fontes: `boards/m5stack-tab5/interface.cpp`, `boards/m5stack-tab5/platformio.ini`,
`src/cardkb2.cpp`, `src/idf/idf_wifi.cpp` do Launcher; `src/unit/unit_Tab5Keyboard.cpp`
da biblioteca M5Unit-KEYBOARD; `src/utility/Power_Class.inl` do M5Unified.
