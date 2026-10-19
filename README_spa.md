<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  🇪🇸 <b>Español</b> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### Nodo eléctrico: lee las tensiones, corrientes, potencia y energía de la red AC y DC de la casa y guarda las reglas para maniobrarla (núcleo probado en el PC y un firmware que compila; nunca ha corrido en una placa)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.4.3-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** El núcleo (las tramas de los medidores PZEM-004T v3 y PZEM-017, el bus que los pregunta, el mensaje del nodo, las entradas analógicas y las reglas de conmutación: 376.195 comprobaciones en 11 programas de test, 375.008 de ellas sobre las reglas de conmutación) se prueba en un ordenador con medidores y contactores de pega, y los mensajes que hace los acepta ARMOR-COMMON. El firmware del nodo (los ajustes, la línea serie, las páginas Medidores y Lecturas de su panel, Bluetooth, actualizaciones firmadas) compila en el contenedor ESP-IDF 5.4 para las dos placas. **Nunca ha corrido en una placa, no se ha conectado nada a un medidor ni a un contactor, y nada de aquí conmuta nada.**

---

## 📋 De un vistazo

| | |
|---|---|
| Versión | `0.4.3` |
| Madurez | Funcional |
| Tests | 379,655 checks in 16 programs (`ctest`) |
| Tecnología | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| En hardware real | Ninguno todavía: ninguna placa montada ni medidor o contactor conectado; nada conmuta nada |

## 🎯 Descripción general

* **Lectura de contadores:** las tramas Modbus RTU de los **PZEM-004T v3** (AC) y **PZEM-017** (DC) de Peacefair: el CRC, la petición que lee los registros y la decodificación de la respuesta (tensión, corriente, potencia, energía, frecuencia, factor de potencia, alarma). Solo se construye la petición de lectura: las que escriben en un contador (su dirección, sus umbrales, el reinicio de su energía) no se construyen en ningún sitio. Se rechaza una respuesta que no cuadra o que trae un valor que ningún contador real podría dar.
* **El bus:** una línea con varios contadores se pregunta de uno en uno, con un tiempo de espera; se guarda la última lectura buena de cada uno y un contador que calla deja de publicarse a los diez segundos.
* **El mensaje** `armor/electrical/<nodo>/state`: una entrada por canal (un circuito, una línea, la entrada de red, un bus DC) con AC o DC, tensión, corriente, potencia, energía, frecuencia, factor de potencia, el estado de un interruptor tal como lo ve el nodo y una alarma; está en el contrato compartido y lleva un estado, nunca una orden.
* **Las reglas para maniobrar,** aparte de cualquier hardware: un controlador del cambio entre dos fuentes que nunca manda las dos, exige la maniobra permitida, el nodo armado justo antes y los dos contactores confirmados abiertos durante todo el tiempo muerto, y convierte un contactor que no muestra lo que se le ordenó en una avería que se queda hasta que se reconoce. **La maniobra está desactivada por defecto y nada maneja ningún hardware.**
* **Las órdenes a un conmutador, sin activar:** el mensaje de estado puede llevar el estado de los conmutadores del nodo, y el contrato compartido tiene una orden (`arm`, y después `close_a` o `close_b` con un token de un solo uso, `open`, `acknowledge`) y la respuesta del nodo; el núcleo las lee y las contesta con las reglas anteriores, probado con los vectores compartidos y con contactores simulados. Ninguna imagen del firmware lo incluye, así que nada puede accionar un contactor, y ARMOR-SERVER se niega a enviar una orden si no se activó (`ARMOR_ELECTRICAL_SWITCHING=1`) y el ACL del broker no lo permite.
* **Dónde se ve:** el *Diseñador eléctrico* de ARMOR-STUDIO dibuja la red de la casa y muestra en cada elemento lo que mide su canal; ARMOR-SERVER guarda las lecturas, su historial y las sumas.
* **El firmware del nodo** (ESP32-S3-WROOM-1 N16R8 por Wi-Fi, o la Waveshare ESP32-S3-ETH por su cable): los ajustes, una línea serie para hasta dieciséis contadores y el panel web de los otros nodos (puesta en marcha, usuarios, Wi-Fi, broker, actualización por aire, HTTPS) con sus propias páginas de Contadores y Lecturas, en ocho idiomas. Solo lee: las reglas para maniobrar no están enlazadas en él. Véase [el firmware](docs/NODE_FIRMWARE.md).
* **Configuración desde el móvil por Bluetooth,** el mismo canal que el del nodo radar: la app ARMOR encuentra el nodo como `ARMOR-XXXXXX` y ajusta su nombre, Wi-Fi, dirección, broker y modo de Bluetooth con los usuarios y el código de puesta en marcha del panel ([el protocolo](docs/BLE_PROVISIONING.md)). Solo escucha mientras el nodo no tiene usuarios, salvo que se indique otra cosa. La parte de radio nunca ha corrido en una placa.
* **Actualizaciones por aire firmadas:** el nodo solo instala desde GitHub una imagen cuyo SHA-256 coincide y que lleva una firma Ed25519 válida del responsable (la clave pública está en el firmware, la privada nunca sale del responsable); un archivo subido a mano por un administrador en el panel se sigue aceptando. Las páginas y mensajes del panel están en ocho idiomas.
* **Emparejamiento Bluetooth con PIN:** el PIN de seis cifras se deriva del código de configuración, se imprime en la consola USB y se muestra a un administrador en el panel; el móvil tiene que escribirlo antes de poder cambiar los ajustes del nodo.
* **Todavía no:** el firmware funcionando en una placa, el hardware, ninguna medida de una instalación real y ninguna maniobra.
* **Solo la casa puede usar el panel:** el panel responde por defecto a la red privada (o a las redes que su dueño indique, como una VPN), rechaza una lista que dejaría fuera a quien la guarda, cuenta las contraseñas erróneas por persona y por dirección, y funciona detrás del proxy del servidor para llegar a él desde fuera de casa sin publicar el nodo.**Las estimaciones lo dicen:** un canal hecho con una entrada de corriente lleva `power_estimated`, así que Studio y el servidor muestran su potencia como estimada (potencia aparente a partir de una corriente y una tensión nominal) y marcan una cifra de red que incluya una.
* **Enlace cifrado opcional con el broker (TLS):** desactivado por defecto, así que el nodo habla MQTT sin cifrar como antes. Un interruptor y una caja para el certificado de la autoridad propia de la instalación (PEM, 2 KB como máximo) en la página Broker hacen que se conecte con `mqtts://`, confíe solo en esa autoridad y compruebe que el certificado del broker se hizo para la dirección a la que llegó; lo que no sea un único certificado PEM se rechaza. Cubierto con pruebas en el ordenador; el handshake aún no se ha ejecutado en una placa.
* **Informe de salud:** cada minuto más o menos el nodo publica cuánto lleva encendido y por qué se reinició la última vez, su memoria libre, su enlace y su señal Wi-Fi, cuántas veces se conectó al broker y el último error que anotó (`armor/health/<nodo>`, el contrato `node_health` de ARMOR-COMMON). El código que lo construye lo comparten todos los nodos y está probado en un ordenador; no se ha ejecutado en una placa.

## 🖼️ Capturas de pantalla

Capturadas a 1920×1080 en un banco de pruebas, con datos de ejemplo y sin ninguna instalación real detrás.

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="Resumen: el estado del nodo de un vistazo - red, broker, firmware y lo que reporta." width="100%"><br><sub>Resumen: el estado del nodo de un vistazo - red, broker, firmware y lo que reporta.</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="Contadores: los contadores de energía y canales analógicos que lee el nodo, cada uno con su nombre y escala." width="100%"><br><sub>Contadores: los contadores de energía y canales analógicos que lee el nodo, cada uno con su nombre y escala.</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="Lecturas: lo que el nodo lee ahora mismo, con la antigüedad de cada valor." width="100%"><br><sub>Lecturas: lo que el nodo lee ahora mismo, con la antigüedad de cada valor.</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Radio Zigbee: el puente serie hacia el módulo coordinador, qué cliente puede usarlo y el estado del puente." width="100%"><br><sub>Radio Zigbee: el puente serie hacia el módulo coordinador, qué cliente puede usarlo y el estado del puente.</sub></td></tr>
</table>

## 🧩 Papel en A.R.M.O.R.

ARMOR-ELECTRICAL mide la red de CA y CC de la casa con sus medidores y guarda las reglas para conmutar entre fuentes. Publica las lecturas para [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER), que guarda su historial y la energía de cada día y genera las alarmas; el menú *Eléctrico* y el *Diseñador eléctrico* de [ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) y la pantalla eléctrica de [ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) lo muestran. La conmutación está diseñada (armar, un token de un solo uso, el enclavamiento, la confirmación de los dos contactores) pero no activada.

## 📂 Estructura del repositorio

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

## 🛠️ Entorno de desarrollo

**Requisitos:** a C++17 compiler for the host tests, the ESP-IDF 5.4 container to build the image, an ESP32-S3-WROOM-1 N16R8 or a Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 379,655 checks: the meters (62), the loop that asks them (72), the settings (85), the readings and inputs (477), Bluetooth (68), the shared base (252), the central link (19), the release and signature checks (20), the interlock (135,736) and the switch sets (239,272)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Véanse el [diseño](docs/DESIGN.md), las [notas de seguridad](docs/SAFETY.md), las [reglas para maniobrar](docs/SWITCHING.md), los [protocolos](docs/PROTOCOLS.md) y los [mensajes](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Proyectos relacionados

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) es un sistema de seguridad perimetral hecho de repositorios independientes. Cada uno tiene su propia versión, sus propias pruebas y su propio README; esta es la familia:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Contratos de mensajes, validadores, vectores de conformidad y tipos generados
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Firmware del nodo de campo para ESP32-S3 con tres radares y su propio panel web
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - Protocolos de inversores y baterías solares y los mensajes de un nodo pasarela
* **ARMOR-ELECTRICAL** (este repositorio) - Nodo eléctrico: contadores, el mensaje de las lecturas de la red y las reglas para maniobrar
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - Nodo y central de alarma: zonas, armado, retardos, sirena y PIN, con el servidor o sin él
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Panel táctil: el estado del sistema en una pantalla de pared, armar y reconocer alarmas, y el hogar del asistente de voz
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - La red local: sus dispositivos, internet y lo que cambia
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordinador central: telemetría, alarmas, dispositivos, lecturas solares y cámaras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Consola web: cámaras, radar, alarmas, energía solar y el diseñador de sitio 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Cliente Android del operador con radar 2D/3D en vivo
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Política de inferencia visual que explica sus decisiones y nunca actúa
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Zonas de cámara, máscaras y reglas de detección para la Jetson Orin NX
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Intenciones de voz sin conexión con una confirmación imposible de falsificar
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Cajas, electrónica y la matriz de aceptación en banco
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Despliegue, el banco de pruebas de la CM5, copias de seguridad y TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Simulador de telemetría sin conexión con fallos repetibles
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Detecta, instala y actualiza los propios repositorios del ecosistema
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Arquitectura, base de seguridad y la matriz de capacidades

## 📚 Documentación y comunidad

Dónde leer más:

* [Matriz de capacidades: qué está probado y qué no](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Catálogo de proyectos: versiones y cómo dependen unos de otros](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Historial de cambios de este repositorio](CHANGELOG.md)
* [Licencia (GPL-3.0-or-later)](LICENSE)
* Preguntas, ideas e informes: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).
