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
  <img src="https://img.shields.io/badge/Checks-135%2C990-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**Verificação de honestidade - o que funciona hoje:** **Maturidade: andaime.** O núcleo (as tramas dos contadores PZEM-004T v3 e PZEM-017, o barramento que os consulta, a mensagem do nó e as regras de comutação: 53 verificações dos contadores e 135 736 das regras de comutação) está testado num computador com contadores e contactores substitutos, e as mensagens que produz são aceites pelo ARMOR-COMMON. O firmware do nó (as definições, a linha série e as páginas Contadores e Leituras do seu painel: mais 71 verificações) compila no contentor ESP-IDF 5.4.2 para as duas placas. **Nunca correu numa placa, nada foi ligado a um contador nem a um contactor, e nada aqui comuta coisa alguma.**

---

## 🎯 Visão geral

* **Ler os contadores:** as tramas Modbus RTU dos **PZEM-004T v3** (AC) e **PZEM-017** (DC) da Peacefair: o CRC, o pedido que lê os registos e a descodificação da resposta (tensão, corrente, potência, energia, frequência, fator de potência, alarme). Só se constrói o pedido de leitura: os que escrevem num contador (o seu endereço, os seus limiares, a reposição da sua energia) não se constroem em lado nenhum. Uma resposta que não encaixa, ou que traz um valor que nenhum contador real poderia dar, recusa-se.
* **O barramento:** uma linha com vários contadores consulta-se um de cada vez, com um limite de tempo; guarda-se a última boa leitura de cada um e um contador que fica em silêncio deixa de se publicar após dez segundos.
* **A mensagem** `armor/electrical/<node>/state`: uma entrada por canal (um circuito, uma linha, a entrada da rede, um barramento DC) com AC ou DC, tensão, corrente, potência, energia, frequência, fator de potência, o estado de um interruptor como o nó o vê e um alarme; está no contrato partilhado e leva um estado, nunca um comando.
* **As regras de comutação,** à parte de qualquer hardware: um controlador para a transferência entre duas fontes que nunca comanda as duas, precisa de a comutação estar permitida, do nó armado mesmo antes, de ambos os contactores confirmados abertos durante todo o tempo morto, e converte um contactor que não mostra o que lhe foi dito numa avaria que fica até ser reconhecida. **A comutação está desativada por omissão e nada aciona hardware algum.**
* **Os comandos a um interruptor, não ativados:** a mensagem de estado pode levar o estado dos interruptores do nó, e o contrato partilhado tem um comando (`arm`, depois `close_a` ou `close_b` com um token de uso único, `open`, `acknowledge`) e a resposta do nó; o núcleo lê-os e responde-lhes com as regras acima, testado contra os vetores partilhados e com contactores substitutos. Nenhuma imagem de firmware o inclui, por isso nada pode acionar um contactor, e o ARMOR-SERVER recusa enviar um comando a menos que se ligue (`ARMOR_ELECTRICAL_SWITCHING=1`) e a ACL do broker o permita.
* **Onde se mostra:** o *Desenhador elétrico* do ARMOR-STUDIO desenha a rede da casa e mostra em cada elemento o que o seu canal mede; o ARMOR-SERVER guarda as leituras, o seu histórico e as somas.
* **O firmware do nó** (ESP32-S3-WROOM-1 N16R8 em Wi-Fi, ou a Waveshare ESP32-S3-ETH no seu cabo): as definições, uma linha série para até dezasseis contadores e o painel web dos outros nós (configuração, utilizadores, Wi-Fi, broker, atualização pelo ar, HTTPS) com as suas próprias páginas Contadores e Leituras, em sete idiomas. Só lê: as regras de comutação não estão ligadas nele. Veja [o firmware](docs/NODE_FIRMWARE.md).
* **Configuração a partir de um telemóvel por Bluetooth,** o mesmo canal que o do nó de radar: a aplicação ARMOR encontra o nó como `ARMOR-XXXXXX` e define o seu nome, Wi-Fi, endereço, broker e modo Bluetooth com os utilizadores do painel e o código de configuração ([o protocolo](docs/BLE_PROVISIONING.md)). Só escuta enquanto o nó não tem utilizador, a menos que se diga o contrário. O lado de rádio nunca correu numa placa.
* **Ainda não:** o firmware a correr numa placa, o hardware, qualquer medição de uma instalação real e qualquer comutação.

## 📂 Estrutura do repositório

```text
ARMOR-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_meters, test_runner, test_config, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ Ambiente de desenvolvimento

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 135,990 checks: the meters (53), the loop that asks them (62), the settings (71), Bluetooth (68), the switching rules (135,736)
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
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Painel tátil: o estado do sistema num ecrã de parede, armar e reconhecer, e a casa do assistente de voz
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - A rede local: os seus dispositivos, a internet e o que muda
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordenador central: telemetria, alarmes, dispositivos, leituras solares e câmaras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Consola web: câmaras, radar, alarmes, energia solar e o desenhador de local 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Cliente Android de operador com um radar 2D/3D em direto
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Política de inferência visual que explica as suas decisões e nunca atua
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
