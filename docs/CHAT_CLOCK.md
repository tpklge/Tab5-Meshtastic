# Chat e relogio

- Em **CHAT**, escolha a conversa no seletor de canais do cabecalho. O mesmo canal filtra o historico e recebe o envio. Rascunhos sao separados por canal durante a sessao.
- Arraste a lista de mensagens para consultar o historico. Novas mensagens recebidas nao deslocam a leitura quando voce esta longe do fim. **Ultimas mensagens** volta ao fim; enviar tambem acompanha a mensagem enviada.
- A lista captura toque e limita os objetos ao historico em memoria (64 mensagens entre todos os canais). O armazenamento anterior continua mantendo as ultimas 30 mensagens entre todos os canais apos reiniciar; mensagens antigas sem indice de canal permanecem no canal 0.
- Em **SET > DATA E HORA**, escolha dia, mes, ano, hora, minuto e fuso UTC e toque em **Salvar data e hora**. A configuracao usa o RTC interno do Tab5, sem GPS ou internet. O fuso inicial e UTC-4 e pode ser ajustado em passos de 15 minutos. Nao ha ajuste automatico de horario de verao.
- O RTC armazena UTC; o fuso fica na NVS. A aplicacao le o RTC ao iniciar. Se ele perdeu alimentacao ou tem dados invalidos, o topo mostra `--:--` ate novo ajuste. O relogio nao altera o horario de mensagens antigas nem configura o radio LoRa.
- O topo mostra a identidade do radio conectado. Antes, `No device` significava simplesmente que o nome estava vazio, inclusive com UART conectado. A identidade agora e atualizada pelo NodeInfo do proprio radio, em qualquer ordem de chegada. Se o radio nao fornecer nome, aparece **Radio sem nome**, com o identificador abaixo.

## Validacao

Teste nativo (macOS, sanitizers):

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk python3 tests/run_chat_tests.py
```

Cobre ordem dos eventos de identidade, snapshots concorrentes do historico, indices de canais, anos bissextos, fusos e limites do RTC. Tambem executa os widgets reais com LVGL sem display: alcance da rolagem, preservacao da posicao de leitura, filtro por canal, rascunhos, limite de objetos, rajadas de mensagens e formulario Wi-Fi/teclado. Requer CMake e compilador C++; no Linux, omita SDKROOT. Validar no Tab5: arrastar uma conversa longa, trocar canais, enviar em ambos, salvar data/hora e reiniciar pela bateria. Nenhum teste instala o firmware.

Referencias: [LVGL 9.5 flags](https://lvgl.io/docs/open/9.5/common-widget-features/flags), [RTC RX8130 no exemplo oficial Tab5](https://github.com/m5stack/M5Tab5-UserDemo/tree/b4e356bc491ca070d54004718dad789c07d5fc93/platforms/tab5/main/hal/utils/rx8130). O driver local preserva VLF (bit 1) durante a inicializacao, rejeita datas invalidas e limpa VLF apenas apos gravar a data/hora completa.

## Wi-Fi e hora da internet

Em **SET > WI-FI**, use **Buscar redes**, selecione uma rede e abra **Configurar rede**. Informe a senha e toque em **Conectar e salvar**. Redes ocultas podem ser digitadas pelo SSID; a senha fica oculta e ha suporte ao teclado fisico e ao teclado na tela. O menu mostra o IP quando conectado.

A ultima rede conectada e salva na NVS e reconectada ao iniciar. **Desligar Wi-Fi** desativa a reconexao automatica mantendo a credencial; **Conectar rede salva** reativa. **Esquecer rede** pede confirmacao, remove a credencial e desliga o Wi-Fi. O menu usa o C6 do Tab5 em 2.4 GHz, com redes abertas ou WPA2/WPA3 pessoais (nao inclui autenticacao empresarial ou portal cativo).

Ao obter IP, a aplicacao consulta `pool.ntp.org` por SNTP. **Sincronizar hora pela internet** repete a consulta. A operacao tem limite de 20 segundos e informa falha de acesso/NTP; ter IP nao garante acesso a internet. A hora recebida e salva no RTC, quando disponivel, mantendo o fuso escolhido. **Aplicar somente o fuso** altera o fuso sem sobrescrever a hora sincronizada. O ajuste manual continua disponivel.

Operacoes de rede rodam em um worker; callbacks de Wi-Fi nao chamam LVGL. Wi-Fi e BLE compartilham a inicializacao idempotente de esp-hosted; desligar Wi-Fi nao desliga o transporte BLE. Validar em hardware: busca, senha incorreta, conexao e IP, NTP, reinicializacao, desligar/reconectar/esquecer e BLE/Wi-Fi simultaneos se usar BLE.

Referencia: [Wi-Fi ESP-IDF 5.4](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/wifi.html); API SNTP conferida no `esp_netif_sntp.h` da instalacao ESP-IDF 5.4.4.

## Busca Wi-Fi e bateria (correcao posterior)

A busca usa `esp_wifi_scan_start(..., false)` e aguarda `WIFI_EVENT_SCAN_DONE`, com limite de 15 segundos. Desligar Wi-Fi ou conectar cancela uma busca pendente. A tarefa de rede roda na CPU0 com prioridade 2, separada do LVGL na CPU1. A leitura do estado pela interface usa tentativa de mutex sem espera; callbacks de rede apenas notificam a tarefa. A tela mostra a etapa e o tempo decorrido para distinguir inicializacao do C6 de uma busca de redes. Os prazos da busca nao interrompem uma chamada RPC interna travada: nesse caso a interface deve permanecer navegavel e indicar ausencia de resposta. A causa exata do travamento fisico ainda depende de validacao no Tab5.

O topo mostra a bateria **do Tab5**, com tensao medida pelo INA226 interno a cada 2 segundos e porcentagem aproximada (prefixo `~`). A estimativa usa uma curva de tensao de bateria Li-ion de duas celulas (6,0-8,4 V); carga conectada, temperatura, desgaste e carregamento afetam a precisao. Nao e contagem de carga. Uma falha transitoria preserva a ultima amostra por ate 15 segundos. Depois disso aparece `--`. Uma tensao valida fora da curva de estimativa continua visivel, com porcentagem `--`. O monitor roda fora do LVGL e nao acessa o conector Grove. Referencia de hardware: [Tab5 oficial](https://docs.m5stack.com/en/core/Tab5).

Teste do fluxo de busca com driver simulado, incluindo timeout, cancelamento e mutex ocupado:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk c++ -std=c++17 -g -fsanitize=address,undefined -Itests/wifi_stubs -Itests/stubs -Imain/app tests/wifi_scan_test.cpp -o /tmp/tab5-wifi-scan-test
/tmp/tab5-wifi-scan-test
```

A lentidao observada **no launcher antes de executar este aplicativo** nao pode ser corrigida neste firmware. Os GPIO53/54 usados pelo UART do RAK tambem sao SDA/SCL do Grove; uma busca I2C do launcher nesse conector e uma hipotese a verificar conforme o launcher e sua versao. Nao mudar tensoes ou pinagem sem verificar o modulo/carrier; o teste comparativo e iniciar o launcher com/sem o RAK, desligando o equipamento antes de mudar a conexao.


## Compatibilidade do INA226

O driver verifica o fabricante `0x5449` e os 12 bits superiores do DIE_ID (`0x226`), permitindo os quatro bits inferiores de revisao. A comparacao anterior com `0x2260` rejeitava a revisao `0x2261`, documentada pela [Texas Instruments](https://e2e.ti.com/support/amplifiers-group/amplifiers/f/amplifiers-forum/1441248/ina226-input-bias-current). Isso e uma causa possivel de indicador vazio; a revisao do aparelho precisa ser confirmada em hardware.

O monitor aguarda a primeira conversao apos inicializar e le somente tensao, independentemente dos registros de corrente/potencia. Uma falha de inicializacao e repetida a cada 5 segundos. Segurar a barra superior abre o diagnostico: `bat:0` indica a ultima leitura sem erro; outro valor hexadecimal identifica a falha do sensor/I2C.

Teste do driver real com I2C simulado:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk c++ -std=c++17 -g -fsanitize=address,undefined -Itests/battery_stubs -Itests/ui_stubs -Itests/stubs -Icomponents/m5_tab5_component/src tests/ina226_driver_test.cpp components/m5_tab5_component/src/drivers/ina226/m5tab5_ina226.cpp -o /tmp/tab5-ina226-test
/tmp/tab5-ina226-test
```

### Monitor que responde com IDs diferentes

Se a leitura dos IDs responde, mas nao identifica o INA226 TI, somente o monitor interno em `0x41` pode usar o modo de compatibilidade de tensao, habilitado explicitamente pelo BSP Tab5. O driver confirma a escrita dos campos de conversao por leitura de retorno, sem reset nem calibracao de corrente. Corrente e potencia ficam indisponiveis nesse modo; a interface usa apenas tensao. Isso segue a abordagem de acesso aos registros do [exemplo oficial M5Stack](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/components/power_monitor_ina226/src/ina226.cpp), que nao exige manufacturer/die IDs. Nao prova a identidade fisica do componente: os valores reais aparecem no diagnostico como `id:MMMM/DDDD`, junto de `bat:`. O teste simulado inclui esse modo e rejeita falha de leitura/confirmacao da configuracao.
