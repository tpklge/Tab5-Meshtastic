# Alterações da sessão — 2026-11-05

## microSD, Wi-Fi e memória

- Histórico migrado do NVS (que apresentou `ESP_ERR_NVS_NOT_ENOUGH_SPACE`) para
  `/sdcard/t5msg.bin`: 1.000 registros de 120 bytes e cabeçalho de 32 bytes.
- microSD em SDSPI/SPI3 a 20 MHz, CLK=43, MOSI=44, MISO=39, CS=42; LDO 4
  associado ao host e mantido até depois do unmount. C6 continua em SDIO.
- Recursos SPI/LDO liberados em falha de mount; cartão ausente mantém RAM-only.
- Espelho do histórico de 120 KB movido da RAM interna para PSRAM, após a serial
  mostrar `failed to create tcpip_thread mbox` seguido de reboot repetido.
- Init antecipado de esp-hosted mantido após alimentação do C6.
- O usuário confirmou busca de redes, conexão Wi-Fi e sincronização NTP.
- LDO isoladamente não resolveu; a hipótese inicial de alimentação como causa
  principal não foi confirmada. A configuração SDMMC antiga não foi retestada
  após a correção de memória.

Detalhes e validações pendentes: [coexistência SD/Wi-Fi](SD_WIFI_COEXISTENCE_PLAN.md).

## BLE, interface e Wi-Fi

- `s_read_pending` convertido para `std::atomic<bool>`, com aquisição via CAS.
- Frames BLE maiores que o buffer de 512 bytes descartados antes da decodificação.
- Cancelamento da troca de transporte restaura o valor anterior guardado no diálogo.
- Listas de chat/nós com fundo opaco; tema escuro explícito e fundo de tela opaco.
- Auto-connect exige rede salva com senha; redes abertas exigem conexão manual.
- Tema LVGL escuro e transições zero registrados nos defaults para novos builds.

## Evidências e limites

- Builds ESP-IDF 5.4.4 concluídos.
- Serial confirmou mount SDSPI, leitura do histórico e UI pronta antes do assert
  de memória; após a correção, janela de 45 segundos sem panic ou reboot.
- Brownout isolado observado durante launcher; investigar se voltar a ocorrer.
- INA226 ID mismatch permanece pendente; avisos de lock LVGL ocorreram no boot.
- Ainda não validada recuperação do histórico após reiniciar, operação prolongada
  com escrita + Wi-Fi, boot frio e resistência a interrupção de energia.
- Não há garantia de atomicidade do arquivo; erros de I/O precisam de tratamento
  mais completo. Dados NVS antigos não foram apagados nem importados automaticamente.

## Decisões mantidas

Backend de mensagens deve ser microSD; SPIFFS foi rejeitado. Cor primária azul
mantida. Secure Boot e criptografia de flash/NVS não foram introduzidos.
