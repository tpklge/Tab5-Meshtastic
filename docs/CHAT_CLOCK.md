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
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk c++ -std=c++17 -g -fsanitize=address,undefined -Itests/stubs -Imain/app -Imain/mesh -Imain/storage tests/chat_clock_test.cpp main/app/app_state.cpp main/app/clock_calendar.cpp -o /tmp/tab5-chat-clock-test
/tmp/tab5-chat-clock-test
```

Cobre ordem dos eventos de identidade, snapshots concorrentes do historico, indices de canais, anos bissextos, fusos e limites do RTC. Validar no Tab5: arrastar uma conversa longa, trocar canais, enviar em ambos, salvar data/hora e reiniciar pela bateria. Nenhum teste instala o firmware.

Referencias: [LVGL 9.5 flags](https://lvgl.io/docs/open/9.5/common-widget-features/flags), [RTC RX8130 no exemplo oficial Tab5](https://github.com/m5stack/M5Tab5-UserDemo/tree/b4e356bc491ca070d54004718dad789c07d5fc93/platforms/tab5/main/hal/utils/rx8130). O driver local preserva VLF (bit 1) durante a inicializacao, rejeita datas invalidas e limpa VLF apenas apos gravar a data/hora completa.
