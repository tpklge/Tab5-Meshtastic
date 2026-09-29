# Canais e QR Meshtastic no Tab5

## Uso

Abra **CANAIS** com o rádio conectado e sincronizado.

- **Reler do radio** consulta os oito slots e a configuração LoRa.
- **Criar** em um slot livre prepara um canal secundário com chave AES-256 nova.
- **Editar** permite nome de até 11 bytes UTF-8, chave Base64, MQTT uplink/downlink
  e desativação de um secundário. **Salvar no radio** grava no nó conectado.
- **Usar no chat** seleciona o índice local para envio. O chat identifica o canal
  de cada mensagem; histórico anterior é considerado canal 0.
- **QR** compartilha um canal; **QR do conjunto** inclui os canais ativos, primário
  primeiro, e LoRa. O QR contém as chaves: só o mostre aos participantes.
- **Ler QR** abre a câmera com prévia, limite de 60 segundos e cancelamento.
- **Importar link** aceita o mesmo conteúdo pelo teclado físico ou na tela.
- Após ler, revise os nomes e fingerprints. **Adicionar** usa slots secundários
  livres e mantém a modulação atual. **Substituir** define o primeiro canal como
  primário e desativa slots restantes. A gravação só começa ao confirmar.
- Na substituição, a opção LoRa aplica preset/modulação/frequência do QR, mantendo
  região, potência e outras políticas locais. Pode reiniciar o rádio; o serviço
  aguarda a reconexão e tenta ler de volta. Se não confirmar, a tela informa isso.

Adicionar canais não muda a frequência: os rádios também precisam usar parâmetros
LoRa compatíveis. A importação é sequencial; se houver falha parcial, os canais já
confirmados permanecem gravados. Releia o rádio para conferir antes de repetir.

## Contrato de protocolo

- `https://meshtastic.org/e/#<base64url>`: protobuf `meshtastic.ChannelSet`.
- `https://meshtastic.org/e/?add=true#<base64url>`: dica para adicionar.
- Decodificação aceita Base64 URL-safe com ou sem padding, de 1 a 8 canais;
  rejeita host incorreto, payload malformado, excesso de tamanho e PSK inválida.
- PSK: 0/1/16/32 bytes. A UI gera 32 bytes com `esp_fill_random`, habilitando
  temporariamente a fonte de entropia SAR do ESP32-P4 (sem ADC de medição no app).
- Secundário sem PSK herda a chave primária. Ao compartilhar um secundário
  sozinho/adicionar um conjunto, a chave herdada é explicitada para não herdar
  acidentalmente outra chave no rádio de destino. `[0]` expressa sem criptografia.
- Administração local usa `MeshPacket` destinado a `my_node_num`, `from=0`,
  `ADMIN_APP`, IDs novos e `get_channel_request=index+1`. O `session_passkey`
  retornado é incluído na escrita. Respostas são correlacionadas por nó e
  `request_id`; a leitura posterior verifica papel, nome, PSK e flags MQTT.
- A operação é vinculada ao nó conectado quando solicitada; trocar de nó a
  interrompe. UART e BLE usam o mesmo serviço administrativo.
- O caminho UART reconhece `FromRadio.rebooted` e refaz o handshake, necessário
  quando uma configuração LoRa reinicia o rádio.
- Chaves e URLs não são enviadas aos logs. Os canais persistem no próprio rádio;
  o Tab5 consulta o conjunto atual.

## Dependências e referências fixadas

- [Meshtastic protobufs](https://github.com/meshtastic/protobufs/tree/adda93634eba450448392c92a1ebe39c4d5deaff),
  commit `adda93634eba450448392c92a1ebe39c4d5deaff`: novos `admin`, `apponly` e
  `connection_status`, gerados por nanopb 0.4.9.1. Protos preexistentes mantidos.
- [M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo/tree/b4e356bc491ca070d54004718dad789c07d5fc93),
  commit `b4e356bc491ca070d54004718dad789c07d5fc93`: componentes `esp_video` 0.7.0,
  `esp_cam_sensor` 0.7.1, `esp_sccb_intf` 0.0.4, `esp_ipa` 0.1.0, com licenças.
  O driver SC202CS desse demo contém a sequência SC2356 do Tab5, 1280x720 RAW8.
  Alterações locais: H264 opcional no CMake/manifest; cmake_utilities permite
  a versão 1.1.1 já usada pelo projeto; espera de captura de vídeo
  limitada a 250 ms para cancelamento. Captura de metadados ISP mantém espera.
- [quirc](https://github.com/dlbeer/quirc/tree/927d680904dc95fdff4cd9d022eb374b438ff8f2),
  commit `927d680904dc95fdff4cd9d022eb374b438ff8f2`, licença ISC: reconhecimento QR.
- LVGL já presente no projeto: widget QR habilitado.
- [Padrão de compartilhamento dos clientes](https://github.com/meshtastic/design/issues/128).

A câmera só inicializa ao abrir o leitor. GPIO36 recebe XCLK de 24 MHz usando
LEDC timer 1/canal 2, preservando timer 0/canal 1 do backlight. SCCB compartilha o
barramento I2C do BSP. Frames RGB565 são reduzidos a 640x360 para quirc; prévia
320x180. Ao fechar, captura para; infraestrutura ISP inicializada é reutilizada.
A prévia e os buffers do driver ficam disponíveis para a próxima leitura.

### Regenerar os novos protobufs

Com checkout do commit acima em `/tmp/tab5-protobufs` e ambiente Python com
`nanopb==0.4.9.1` e `grpcio-tools`:

```sh
python -m grpc_tools.protoc -I/tmp/tab5-protobufs \
  --plugin=protoc-gen-nanopb=/caminho/venv/bin/protoc-gen-nanopb \
  --nanopb_out=-I/tmp/tab5-protobufs:components/meshtastic_protos/proto \
  /tmp/tab5-protobufs/meshtastic/admin.proto \
  /tmp/tab5-protobufs/meshtastic/apponly.proto \
  /tmp/tab5-protobufs/meshtastic/connection_status.proto
```

## Validação

`tests/run_channels_tests.py` compila testes nativos com AddressSanitizer e
UndefinedBehaviorSanitizer. Requer compilador C/C++ e pacote Python `qrcode`.
Em Macs com SDK padrão incompatível com o linker, selecione um SDK instalado:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  python tests/run_channels_tests.py
```

Cobertura: fixture pública padrão, fixture de oito canais produzida com o
protobuf oficial/Google, URL com/sem padding, PSK, UTF-8, entradas inválidas,
canal do pacote de chat, chave herdada, leitura de QR produzido por Python,
rádio simulado com sessão administrativa/correlação, leitura de confirmação,
recusa de escrita, proteção do primário, capacidade e adicionar/substituir.

Ainda exige aparelho: câmera real/iluminação/distância, interoperabilidade
Android/iOS por câmera, gravação persistente no firmware do rádio, reconexão
após alteração LoRa, BLE, áudio/backlight durante leitura e cold boot.

Identidade e edição geral de rádio não fazem parte desta etapa; configurações
LoRa aqui são somente as recebidas no compartilhamento de canais.
