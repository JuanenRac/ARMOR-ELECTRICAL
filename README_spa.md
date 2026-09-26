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
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nodo eléctrico: lee las tensiones, corrientes, potencia y energía de la red AC y DC de la casa y guarda las reglas para maniobrarla (núcleo probado en el PC; el firmware aún está por hacer)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C789-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** El núcleo (las tramas de los contadores PZEM-004T v3 y PZEM-017, el bus que los pregunta, el mensaje del nodo y las reglas para maniobrar: 53 comprobaciones de los contadores y 135.736 de las reglas de maniobra) se prueba en un ordenador con contadores y contactores simulados, y ARMOR-COMMON acepta los mensajes que hace. **Aún no hay firmware, no se ha conectado nada a un contador ni a un contactor, y nada de aquí maniobra nada.**

---

## 🎯 Descripción general

* **Lectura de contadores:** las tramas Modbus RTU de los **PZEM-004T v3** (AC) y **PZEM-017** (DC) de Peacefair: el CRC, la petición que lee los registros y la decodificación de la respuesta (tensión, corriente, potencia, energía, frecuencia, factor de potencia, alarma). Solo se construye la petición de lectura: las que escriben en un contador (su dirección, sus umbrales, el reinicio de su energía) no se construyen en ningún sitio. Se rechaza una respuesta que no cuadra o que trae un valor que ningún contador real podría dar.
* **El bus:** una línea con varios contadores se pregunta de uno en uno, con un tiempo de espera; se guarda la última lectura buena de cada uno y un contador que calla deja de publicarse a los diez segundos.
* **El mensaje** `armor/electrical/<nodo>/state`: una entrada por canal (un circuito, una línea, la entrada de red, un bus DC) con AC o DC, tensión, corriente, potencia, energía, frecuencia, factor de potencia, el estado de un interruptor tal como lo ve el nodo y una alarma; está en el contrato compartido y lleva un estado, nunca una orden.
* **Las reglas para maniobrar,** aparte de cualquier hardware: un controlador del cambio entre dos fuentes que nunca manda las dos, exige la maniobra permitida, el nodo armado justo antes y los dos contactores confirmados abiertos durante todo el tiempo muerto, y convierte un contactor que no muestra lo que se le ordenó en una avería que se queda hasta que se reconoce. **La maniobra está desactivada por defecto y nada maneja ningún hardware.**
* **Dónde se ve:** el *Diseñador eléctrico* de ARMOR-STUDIO dibuja la red de la casa y muestra en cada elemento lo que mide su canal; ARMOR-SERVER guarda las lecturas, su historial y las sumas.
* **Todavía no:** el firmware del nodo (Wi-Fi, panel, MQTT, puertos serie), el hardware, ninguna medida de una instalación real y ninguna maniobra.

## 📂 Estructura del repositorio

```text
ARMOR-ELECTRICAL/
├── core/    pzem.hpp (frames of the PZEM meters), pzem_bus.hpp (the line), electrical_json.hpp (the message), interlock.hpp (the rules for switching), json.hpp
├── tests/   test_meters.cpp, test_interlock.cpp, emit_samples.cpp + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ Entorno de desarrollo

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # the meters (53 checks) and the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
```

Véanse el [diseño](docs/DESIGN.md), las [notas de seguridad](docs/SAFETY.md), las [reglas para maniobrar](docs/SWITCHING.md), los [protocolos](docs/PROTOCOLS.md) y los [mensajes](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Proyectos relacionados

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) es un sistema de seguridad perimetral hecho de repositorios independientes. Cada uno tiene su propia versión, sus propias pruebas y su propio README; esta es la familia:

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contratos de mensajes, validadores, vectores de conformidad y tipos generados
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Firmware del nodo de campo para ESP32-S3 con tres radares y su propio panel web
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - Protocolos de inversores y baterías solares y los mensajes de un nodo pasarela
* **ARMOR-ELECTRICAL** (este repositorio) - Nodo eléctrico: contadores, el mensaje de las lecturas de la red y las reglas para maniobrar
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinador central: telemetría, alarmas, dispositivos, lecturas solares y cámaras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Consola web: cámaras, radar, alarmas, energía solar y el diseñador de sitio 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Cliente Android del operador con radar 2D/3D en vivo
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Política de inferencia visual que explica sus decisiones y nunca actúa
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intenciones de voz sin conexión con una confirmación imposible de falsificar
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Cajas, electrónica y la matriz de aceptación en banco
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Despliegue, el banco de pruebas de la CM5, copias de seguridad y TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulador de telemetría sin conexión con fallos repetibles
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Arquitectura, base de seguridad y la matriz de capacidades

## 📚 Documentación y comunidad

Dónde leer más:

* [Matriz de capacidades: qué está probado y qué no](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catálogo de proyectos: versiones y cómo dependen unos de otros](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Historial de cambios de este repositorio](CHANGELOG.md)
* [Licencia (GPL-3.0-or-later)](LICENSE)
* Preguntas, ideas e informes: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).
