# Tab5 e Launcher 2.8.0

Codigo analisado: `bmorcelli/Launcher` tag `2.8.0`, commit `4150335`.
O patch em `patches/launcher-2.8.0-tab5-keyboard-rak.patch` aplica-se a esse
commit. Ele nao e um firmware OTA da nossa aplicacao.

No Launcher 2.8.0, `_setup_gpio()` chama `launcherWifiInitHostedSdio()` antes de
`tab5KbSetup()`. Se a inicializacao do C6 demora, falha ou reinicia, o teclado
interno A164 nao e inicializado a tempo. O teclado usa GPIO0/1 e interrupcao
GPIO50 no modo Normal, mas a versao 2.8.0 cria o barramento Arduino `Wire1`.
Na branch atual do Launcher, a mesma rotina usa `Wire`, nao `Wire1`.

O patch de compatibilidade move a inicializacao do A164 antes do ESP-Hosted,
usa `Wire` e desabilita a busca do CardKB2 no Grove GPIO53/54, ocupado pelo
RAK UART neste equipamento. Essa ultima mudanca remove suporte ao CardKB2
externo **somente no Launcher customizado**; nao altera nossa aplicacao.

O mesmo patch corrige o fluxo Wi-Fi da versao 2.8.0: depois de um timeout, a
opcao Retry cancela a tentativa anterior e inicia outra; Change Password
permite substituir uma senha salva no Launcher. O prazo de conexao sobe para
20 segundos e a falha mostra o codigo numerico de desconexao ou a ausencia de
IP. O log nao imprime mais a senha digitada. A rede que falhou no Launcher
conecta normalmente pela aplicacao, o que orienta o diagnostico para esse
fluxo, mas a correcao ainda precisa de validacao no hardware.

Depois da primeira gravacao, Change Password exibiu um SSID com caracteres
invalidos e motivo 201 (rede nao encontrada). A causa era `wifiConnect` receber
o SSID por referencia a uma opcao do menu que a propria funcao substituia no
erro. O patch agora guarda o SSID por valor. Tambem usa o mesmo limiar WPA2 e
PMF da aplicacao para redes com senha, espera a desconexao anterior por 250 ms
e distingue ausencia de evento de desconexao do motivo 1.

O Launcher 2.8.0 nao tem a chave NVS `launcher/hosted_st` encontrada na branch
atual. Nossa aplicacao nao deve altera-la para tentar recuperar essa versao.
Ela continua usando o modo Character do A164 enquanto roda e restaura modo
Normal, configuracao de interrupcao e fila ao sair.

O botao SAIR da aplicacao entra em sono profundo sem temporizador nem fontes
de wakeup configuradas, apaga a tela e desliga C6 e alimentacao externa. Isso
evita o pulso `PWROFF_PLUSE`, que pode religar a placa imediatamente com USB
conectado. E uma suspensao de baixo consumo, nao um corte fisico de energia;
o reset fisico do Tab5 inicia o launcher novamente.

Fontes: `boards/m5stack-tab5/interface.cpp`, `boards/m5stack-tab5/platformio.ini`
e `src/idf/idf_wifi.cpp` na tag 2.8.0; comparacao com a branch `main` do
Launcher e com `M5Unit-KEYBOARD`.
