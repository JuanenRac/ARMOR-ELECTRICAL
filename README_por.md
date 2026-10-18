<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  🇵🇹 <b>Português</b>
</p>

### Nó elétrico: lê as tensões, correntes, potência e energia da rede AC e DC da casa e guarda as regras para a comutar (núcleo testado em anfitrião e um firmware que compila; nunca correu numa placa)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.3.8-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**Verificação de honestidade - o que funciona hoje:** O núcleo (as tramas dos contadores PZEM-004T v3 e PZEM-017, o barramento que os interroga, a mensagem do nó, as entradas analógicas e as regras de comutação: 376 195 verificações em 11 programas de teste, 375 008 delas sobre as regras de comutação) é testado num computador com contadores e contactores de substituição, e as mensagens que produz são aceites pelo ARMOR-COMMON. O firmware do nó (as definições, a linha série, as páginas Contadores e Leituras do seu painel, Bluetooth, atualizações assinadas) compila no contentor ESP-IDF 5.4 para as duas placas. **Nunca correu numa placa, nada foi ligado a um contador nem a um contactor, e nada aqui comuta coisa alguma.**

---

## 📋 Num relance

| | |
|---|---|
| Versão | `0.3.8` |
| Maturidade | Funcional |
| Testes | 376,272 checks in 11 programs (`ctest`) |
| Tecnologia | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| Em hardware real | Nenhum ainda: nenhuma placa montada nem contador ou contactor ligados; nada comuta coisa alguma |

## 🎯 Visão geral

* **Ler os contadores:** as tramas Modbus RTU dos **PZEM-004T v3** (AC) e **PZEM-017** (DC) da Peacefair: o CRC, o pedido que lê os registos e a descodificação da resposta (tensão, corrente, potência, energia, frequência, fator de potência, alarme). Só se constrói o pedido de leitura: os que escrevem num contador (o seu endereço, os seus limiares, a reposição da sua energia) não se constroem em lado nenhum. Uma resposta que não encaixa, ou que traz um valor que nenhum contador real poderia dar, recusa-se.
* **O barramento:** uma linha com vários contadores consulta-se um de cada vez, com um limite de tempo; guarda-se a última boa leitura de cada um e um contador que fica em silêncio deixa de se publicar após dez segundos.
* **A mensagem** `armor/electrical/<node>/state`: uma entrada por canal (um circuito, uma linha, a entrada da rede, um barramento DC) com AC ou DC, tensão, corrente, potência, energia, frequência, fator de potência, o estado de um interruptor como o nó o vê e um alarme; está no contrato partilhado e leva um estado, nunca um comando.
* **As regras de comutação,** à parte de qualquer hardware: um controlador para a transferência entre duas fontes que nunca comanda as duas, precisa de a comutação estar permitida, do nó armado mesmo antes, de ambos os contactores confirmados abertos durante todo o tempo morto, e converte um contactor que não mostra o que lhe foi dito numa avaria que fica até ser reconhecida. **A comutação está desativada por omissão e nada aciona hardware algum.**
* **Os comandos a um interruptor, não ativados:** a mensagem de estado pode levar o estado dos interruptores do nó, e o contrato partilhado tem um comando (`arm`, depois `close_a` ou `close_b` com um token de uso único, `open`, `acknowledge`) e a resposta do nó; o núcleo lê-os e responde-lhes com as regras acima, testado contra os vetores partilhados e com contactores substitutos. Nenhuma imagem de firmware o inclui, por isso nada pode acionar um contactor, e o ARMOR-SERVER recusa enviar um comando a menos que se ligue (`ARMOR_ELECTRICAL_SWITCHING=1`) e a ACL do broker o permita.
* **Onde se mostra:** o *Desenhador elétrico* do ARMOR-STUDIO desenha a rede da casa e mostra em cada elemento o que o seu canal mede; o ARMOR-SERVER guarda as leituras, o seu histórico e as somas.
* **O firmware do nó** (ESP32-S3-WROOM-1 N16R8 em Wi-Fi, ou a Waveshare ESP32-S3-ETH no seu cabo): as definições, uma linha série para até dezasseis contadores e o painel web dos outros nós (configuração, utilizadores, Wi-Fi, broker, atualização pelo ar, HTTPS) com as suas próprias páginas Contadores e Leituras, em oito idiomas. Só lê: as regras de comutação não estão ligadas nele. Veja [o firmware](docs/NODE_FIRMWARE.md).
* **Configuração a partir de um telemóvel por Bluetooth,** o mesmo canal que o do nó de radar: a aplicação ARMOR encontra o nó como `ARMOR-XXXXXX` e define o seu nome, Wi-Fi, endereço, broker e modo Bluetooth com os utilizadores do painel e o código de configuração ([o protocolo](docs/BLE_PROVISIONING.md)). Só escuta enquanto o nó não tem utilizador, a menos que se diga o contrário. O lado de rádio nunca correu numa placa.
* **Atualizações pelo ar assinadas:** o nó só instala do GitHub uma imagem cujo SHA-256 coincide e que leva uma assinatura Ed25519 válida do responsável (a chave pública está no firmware, a privada nunca sai do responsável); um ficheiro carregado à mão por um administrador no painel continua a ser aceite. As páginas e mensagens do painel estão em oito idiomas.
* **Emparelhamento Bluetooth com PIN:** o PIN de seis dígitos deriva do código de configuração, é impresso na consola USB e mostrado a um administrador no painel; o telemóvel tem de o introduzir antes de poder escrever as definições do nó.
* **Ainda não:** o firmware a correr numa placa, o hardware, qualquer medição de uma instalação real e qualquer comutação.
* **Só a casa pode usar o painel:** o painel responde por omissão à rede privada (ou às redes que o dono indicar, como uma VPN), recusa uma lista que deixaria de fora quem a guarda, conta as palavras-passe erradas por pessoa e por endereço e funciona atrás do proxy do servidor para ser alcançado de fora de casa sem publicar o nó.**As estimativas dizem-no:** um canal feito a partir de uma entrada de corrente leva `power_estimated`, por isso o Studio e o servidor mostram a sua potência como estimada (potência aparente a partir de uma corrente e uma tensão nominal) e marcam um valor de rede que inclua uma.
* **Ligação cifrada opcional ao broker (TLS):** desativada por predefinição, por isso o nó fala MQTT sem cifra como antes. Um interruptor e uma caixa para o certificado da autoridade própria da instalação (PEM, no máximo 2 KB) na página Broker fazem-no ligar-se com `mqtts://`, confiar só nessa autoridade e verificar que o certificado do broker foi feito para o endereço alcançado; o que não for um único certificado PEM é recusado. Coberto por testes no computador; o handshake ainda não foi executado numa placa.
* **Relatório de saúde:** cerca de uma vez por minuto o nó publica há quanto tempo está ligado e porque reiniciou da última vez, a memória livre, a ligação e o sinal Wi-Fi, quantas vezes se ligou ao broker e o último erro que registou (`armor/health/<nó>`, o contrato `node_health` do ARMOR-COMMON). O código que o constrói é partilhado por todos os nós e testado num computador; nunca correu numa placa.

## 🖼️ Capturas de ecrã

Capturadas a 1920×1080 numa bancada de testes, com dados de exemplo e sem qualquer instalação real por trás.

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="Resumo: o estado do nó num relance - rede, broker, firmware e o que reporta." width="100%"><br><sub>Resumo: o estado do nó num relance - rede, broker, firmware e o que reporta.</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="Contadores: os contadores de energia e canais analógicos que o nó lê, cada um com o seu nome e escala." width="100%"><br><sub>Contadores: os contadores de energia e canais analógicos que o nó lê, cada um com o seu nome e escala.</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="Leituras: o que o nó está a ler neste momento, com a idade de cada valor." width="100%"><br><sub>Leituras: o que o nó está a ler neste momento, com a idade de cada valor.</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Rádio Zigbee: a ponte série para o módulo coordenador, que cliente a pode usar e o estado da ponte." width="100%"><br><sub>Rádio Zigbee: a ponte série para o módulo coordenador, que cliente a pode usar e o estado da ponte.</sub></td></tr>
</table>

## 🧩 Papel no A.R.M.O.R.

O ARMOR-ELECTRICAL mede a rede CA e CC da casa com os seus contadores e guarda as regras para comutar entre fontes. Publica as leituras para o [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER), que guarda o histórico e a energia de cada dia e gera os alarmes; o menu *Elétrico* e o *Desenhador elétrico* do [ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) e o ecrã elétrico do [ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) mostram-no. A comutação está desenhada (armar, um token de uso único, o intertravamento, a confirmação dos dois contactores) mas não ativada.

## 📂 Estrutura do repositório

```text
ARMOR-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (8 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_meters, test_runner, test_config, test_mqtt_tls, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ Ambiente de desenvolvimento

**Requisitos:** a C++17 compiler for the host tests, the ESP-IDF 5.4 container to build the image, an ESP32-S3-WROOM-1 N16R8 or a Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 376,272 checks: the meters (62), the loop that asks them (72), the settings (85), the readings and inputs (477), Bluetooth (68), the shared base (252), the central link (19), the release and signature checks (20), the interlock (135,736) and the switch sets (239,272)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

Veja o [guia do firmware](docs/NODE_FIRMWARE.md) e o [canal Bluetooth](docs/BLE_PROVISIONING.md).

Veja o [desenho](docs/DESIGN.md), as [notas de segurança](docs/SAFETY.md), as [regras de comutação](docs/SWITCHING.md), os [protocolos](docs/PROTOCOLS.md) e as [mensagens](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Projetos relacionados

O **A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) é um sistema de segurança perimetral feito de repositórios independentes. Cada um tem a sua própria versão, os seus próprios testes e o seu próprio README; esta é a família:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Contratos de mensagens, validadores, vetores de conformidade e tipos gerados
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Firmware de nó de campo para ESP32-S3 com três radares e o seu próprio painel web
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - Protocolos de inversores e baterias solares e as mensagens de um nó de ligação
* **ARMOR-ELECTRICAL** (este repositório) - Nó elétrico: contadores, a mensagem das leituras da rede e as regras de comutação
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - Nó e central de alarme: zonas, armar, atrasos, sirene e PIN, com o servidor ou sem ele
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Painel tátil: o estado do sistema num ecrã de parede, armar e reconhecer, e a casa do assistente de voz
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - A rede local: os seus dispositivos, a internet e o que muda
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordenador central: telemetria, alarmes, dispositivos, leituras solares e câmaras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Consola web: câmaras, radar, alarmes, energia solar e o desenhador de local 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Cliente Android de operador com um radar 2D/3D em direto
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Política de inferência visual que explica as suas decisões e nunca atua
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Zonas de câmara, máscaras e regras de deteção para o Jetson Orin NX
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Intenções de voz offline com uma confirmação que não se pode falsificar
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Caixas, eletrónica e a matriz de aceitação de bancada
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Implementação, a bancada de testes CM5, cópias de segurança e TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Simulador de telemetria offline com falhas repetíveis
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Deteta, instala e atualiza os próprios repositórios do ecossistema
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Arquitetura, linha de base de segurança e a matriz de capacidades

## 📚 Documentação e comunidade

Onde ler mais:

* [Matriz de capacidades: o que está provado e o que não](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Catálogo de projetos: versões e como os repositórios dependem uns dos outros](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Registo de alterações deste repositório](CHANGELOG.md)
* [Licença (GPL-3.0-or-later)](LICENSE)
* Perguntas, ideias e relatórios: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENÇA

GPL-3.0-or-later - veja [LICENSE](LICENSE).
