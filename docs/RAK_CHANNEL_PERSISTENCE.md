# Perda de canais no RAK3172H

Analisado em 2026-09-29: `tpklge/RAK3172h-firmware`, commit
`a6641d3dc86b56bf1e9b714fc24b4b1c5f086231` (base Meshtastic 2.7.26).

## Defeito reproduzido

`AdminModule::handleSetChannel` atualiza a RAM e chama `saveChanges(SEGMENT_CHANNELS, false)`.
`NodeDB::saveChannelsToDisk` utiliza `SafeFile` com `fullAtomic=true`.
No STM32, `SafeFile::close` chama `renameFile`, que usava `copyFile`.
A implementação STM32 de `FILE_O_WRITE` cria/abre o arquivo e posiciona no **fim**.
Assim, a cópia não substitui `channels.proto`: concatena o protobuf novo ao antigo.

Um arquivo anterior com oito canais + outro com oito canais produz `array overflow`
no decoder nanopb limitado a oito entradas. Ao carregar esse arquivo,
`NodeDB::loadFromDisk` instala os canais padrão. A consulta administrativa logo
após salvar lê RAM, portanto pode confirmar uma alteração cuja persistência falhou.
Além disso, `saveProto` ignorava o retorno falso de `SafeFile::close` no valor retornado.

A reconexão `want_config_id` não recarrega preferências do disco nem apaga canais;
ela envia os oito canais em RAM. Portanto, o defeito de persistência está comprovado,
mas ainda não demonstra sozinho a perda ao reiniciar *apenas* o cliente. É necessário
verificar se o RAK também reinicia durante a troca de aplicação/alimentação Grove.
A inicialização do expansor do Tab5 faz soft reset e depois habilita EXT5V;
manter o cabo conectado não comprova alimentação contínua. Isso é uma hipótese,
não uma medição elétrica nem confirmação de reinício do RAK.

## Correção

Aplicar `patches/rak3172-channel-persistence.patch` na raiz do repositório do RAK:

```sh
git apply --check /caminho/Tab5Meshtastic/patches/rak3172-channel-persistence.patch
git apply /caminho/Tab5Meshtastic/patches/rak3172-channel-persistence.patch
pio run -e rak3172
```

- Usa `STM32_LittleFS::rename`, que já encapsula `lfs_rename`, para substituir o destino.
- Remove temporário residual antes de abrir para escrita, evitando novo acréscimo.
- Retorna falha de `saveProto` quando o fechamento/verificação/substituição falha.

A correção pertence ao firmware do **RAK**, não ao binário OTA do Tab5.
Não recupera chaves que já tenham sido perdidas; depois da atualização, recriar ou
importar os canais a partir de um QR/backup disponível. Não apagar toda a flash.

## Validação sem hardware

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  python3 tests/run_rak_persistence_tests.py /tmp/tab5-rak3172-source
```

O checkout passado deve conter a correção e ter o commit original em HEAD.
O teste compila os corpos reais de `SafeFile`, `copyFile` e `renameFile`, com um
filesystem em memória reproduzindo o modo append do STM32 e o schema nanopb real
`ChannelFile` do RAK. Executa original e corrigido com ASan/UBSan:

- Original: concatenação reproduzida, decode falha com `array overflow`.
- Corrigido: oito canais e nome do canal preservados após reabrir o arquivo.
- Temporário residual: próxima gravação limpa o resíduo.
- Falhas de rename/remoção: arquivo anterior preservado, operação retorna falha.

Não testa fisicamente flash, alimentação nem firmware em execução. Validar no RAK:
salvar um canal, reiniciar aplicativo, reler; em seguida reiniciar também o rádio e
reler novamente. Nenhum teste abre serial, grava dispositivos ou publica no GitHub.

## Compilação realizada

`pio run -e rak3172`: SUCCESS, sem gravação de dispositivo.
RAM: consultar `build/rak3172/build.log`; flash reportada: 173636 / 233472 bytes.
Binário: `build/rak3172/firmware-rak3172-2.7.26-channel-persistence.bin` (174012 bytes).
SHA-256: `deec884b355b377d62793061e9b5cfa6d160b1654ecc9839818a20d6fcc036f2`.
Base: commit RAK `a6641d3` + patch registrado no projeto Tab5 em `f3273c0`.
A versão interna continua `2.7.26.a6641d3`; identificar a correção pelo nome/hash
acima. O arquivo é para o RAK3172H via ST-Link em `0x08000000`, conforme o README
do rádio. **Não é OTA do Tab5.** Validado cabeçalho vetorial STM32 e limite de flash.
