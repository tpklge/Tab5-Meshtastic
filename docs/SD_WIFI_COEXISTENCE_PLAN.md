# Coexistência microSD + Wi-Fi no M5Tab5

Estado em 2026-11-05: microSD em SDSPI/SPI3 com LDO 4; Wi-Fi do C6
mantido em SDIO. O usuário confirmou busca de redes, conexão e NTP.

## Solução atual

- `main/storage/sd_storage.cpp`: `SDSPI_HOST_DEFAULT()`, `SPI3_HOST`, 20 MHz,
  DMA automático e `esp_vfs_fat_sdspi_mount()` em `/sdcard`.
- Pinos: CLK=43, MOSI=44 (CMD), MISO=39 (D0), CS=42 (D3).
- `sd_pwr_ctrl_new_on_chip_ldo()` adquire o canal 4 e associa o controlador a
  `host.pwr_ctrl_handle`. A liberação ocorre em falha de inicialização/mount
  ou **depois** de um unmount bem-sucedido. Não desligar o LDO antes do unmount.
- C6 permanece no SDMMC slot 1, GPIOs 8–13. O cartão não usa mais o SDMMC.
- Dependências: `sdmmc`, `esp_driver_spi`, `esp_driver_sdspi`; o
  `esp_driver_sdmmc` continua necessário pelo esp-hosted.
- `main/storage/message_store.cpp`: arquivo `/sdcard/t5msg.bin`, cabeçalho de
  32 bytes e ring de 1.000 registros de 120 bytes; espelho de 120.000 bytes
  explicitamente alocado em PSRAM, sem consumir essa quantidade de RAM interna.
- Sem formatação automática. Cartão ausente ou mount inválido mantém operação
  em RAM, sem persistência. Isso é registrado no log, sem indicação dedicada na UI.

## Diagnóstico e tentativas anteriores

1. SDMMC no slot 0 e mudanças na ordem de inicialização não eliminaram o sintoma.
2. Foi adicionado o LDO 4, seguindo o BSP Tab5 disponível em
   `../TAB5_ERP/firmware/managed_components/espressif__m5stack_tab5/src/bsp_storage.c`.
   Também foram desativados os pull-ups internos no caminho SDMMC. O usuário
   informou que a tela azul continuou; o LDO isoladamente não resolveu.
3. O cartão foi migrado para SDSPI/SPI3, mantendo LDO 4 e formato do histórico.
4. A serial mostrou montagem do cartão de 59344 MB, leitura do histórico e
   `UI ready`, seguidos repetidamente por:

   ```text
   assert failed: tcpip_init ... (failed to create tcpip_thread mbox)
   Rebooting...
   rst:0xc (SW_CPU_RESET)
   ```

5. O mapa do linker confirmou `s_ring` com 120.000 bytes em RAM interna.
   Esse buffer foi movido para PSRAM para liberar memória às filas e tarefas.
6. Após a correção, não houve panic/reboot na janela monitorada de 45 segundos;
   o usuário confirmou busca, conexão Wi-Fi e sincronização NTP.

A hipótese original de queda de alimentação causada pela ausência do LDO
**não foi confirmada**. Um brownout foi capturado no início, durante a transição
pelo launcher, mas o reboot repetido da aplicação tinha o assert de alocação
acima. A configuração SDMMC antiga não foi retestada após mover o buffer para
PSRAM; portanto não se conclui que compartilhar SDMMC fosse a causa original.

## Validação restante

- Receber mensagens, reiniciar e verificar recuperação do histórico.
- Gravar no cartão durante tráfego Wi-Fi/NTP e testar o ring após 1.000 mensagens.
- Boot frio pela bateria e pelo launcher, com e sem cartão.
- Investigar alimentação caso novos brownouts ocorram.
- Melhorar checagem de erros de I/O e recuperação de gravações interrompidas:
  registro e cabeçalho separados não garantem atomicidade contra perda de energia.

## Alternativas do plano original

SPIFFS foi rejeitado pelo usuário como backend de mensagens e não foi adotado.
Montar/desmontar SD conforme o Wi-Fi também não foi adotado. Não são etapas
pendentes. O init antecipado de esp-hosted foi mantido, mas não é requisito de
compartilhamento de barramento para o cartão em SPI3.

## Distribuição

Usar o app `tab5_meshtastic_v2.bin` no Launcher, em partição com espaço suficiente.
O build de referência tem 1.664.560 bytes e excede a antiga partição de 1600 KiB
(1.638.400 bytes); conferir o tamanho real nos checksums/metadados da release.
Não usar a imagem standalone para atualizar uma instalação do Launcher.
