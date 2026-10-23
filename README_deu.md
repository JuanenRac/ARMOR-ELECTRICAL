<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-NODE-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-NODE-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### Elektroknoten: liest Spannungen, Ströme, Leistung und Energie des AC- und DC-Netzes des Hauses und hält die Regeln fürs Schalten (am Rechner getesteter Kern und eine Firmware, die baut; sie lief noch nie auf einer Platine)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.4.6-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**Ehrlichkeitsprüfung - was heute läuft:** Der Kern (die Rahmen der Zähler PZEM-004T v3 und PZEM-017, der Bus, der sie abfragt, die Nachricht des Knotens, die analogen Eingänge und die Schaltregeln: 380.189 Prüfungen in 16 Testprogrammen, davon 375.036 zu den Schaltregeln) wird auf einem Computer mit Ersatzzählern und -schützen getestet, und die von ihm erzeugten Nachrichten akzeptiert ARMOR-COMMON. Die Firmware des Knotens (die Einstellungen, die serielle Leitung, die Seiten Zähler und Messwerte seines Panels, Bluetooth, signierte Updates) baut im ESP-IDF-5.5.5-Container für beide Platinen. **Sie lief nie auf einer Platine, an keinen Zähler und kein Schütz wurde etwas angeschlossen, und nichts hier schaltet irgendetwas.**

---

## 📋 Auf einen Blick

| | |
|---|---|
| Version | `0.4.6` |
| Reifegrad | Funktionsfähig |
| Tests | 380.189 Prüfungen in 16 Programmen (`ctest`) |
| Technik | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| Auf echter Hardware | Noch keiner: keine Platine aufgebaut und kein Zähler oder Schütz angeschlossen; nichts schaltet irgendetwas |

## 🎯 Überblick

* **Zähler lesen:** die Modbus-RTU-Rahmen der Peacefair-Zähler **PZEM-004T v3** (AC) und **PZEM-017** (DC): die CRC, die Anfrage, die die Register liest, und die Auswertung der Antwort (Spannung, Strom, Leistung, Energie, Frequenz, Leistungsfaktor, Alarm). Nur die Leseanfrage wird gebaut: die schreibenden (Adresse, Schwellen, Zurücksetzen der Energie) werden nirgends gebaut. Eine Antwort, die nicht passt oder einen Wert enthält, den kein echter Zähler liefern könnte, wird abgelehnt.
* **Der Bus:** eine Leitung mit mehreren Zählern wird einzeln abgefragt, mit Zeitlimit; der letzte gute Wert jedes Zählers wird behalten, und ein Zähler, der schweigt, wird nach zehn Sekunden nicht mehr veröffentlicht.
* **Die Nachricht** `armor/electrical/<Knoten>/state`: ein Eintrag je Kanal (ein Stromkreis, eine Leitung, der Netzanschluss, ein DC-Bus) mit AC oder DC, Spannung, Strom, Leistung, Energie, Frequenz, Leistungsfaktor, dem Zustand eines Schalters, wie ihn der Knoten sieht, und einem Alarm; sie steht im gemeinsamen Vertrag und trägt einen Zustand, nie einen Befehl.
* **Die Regeln fürs Schalten,** unabhängig von jeder Hardware: ein Regler für den Wechsel zwischen zwei Quellen, der nie beide ansteuert, das erlaubte Schalten, den kurz zuvor scharfgeschalteten Knoten und beide Schütze über die ganze Totzeit als offen bestätigt verlangt und ein Schütz, das nicht zeigt, was ihm befohlen wurde, zu einer Störung macht, die bis zur Quittierung bleibt. **Schalten ist standardmäßig aus, und nichts steuert irgendeine Hardware.**
* **Die Befehle an einen Schalter, nicht aktiviert:** Die Zustandsnachricht kann den Zustand der Schalter des Knotens tragen, und der gemeinsame Vertrag kennt einen Befehl (`arm`, dann `close_a` oder `close_b` mit einem Einmal-Token, `open`, `acknowledge`) und die Antwort des Knotens; der Kern liest und beantwortet sie mit den obigen Regeln, geprüft mit den gemeinsamen Vektoren und mit simulierten Schützen. Kein Firmware-Abbild enthält es, also kann nichts ein Schütz ansteuern, und ARMOR-SERVER sendet keinen Befehl, solange er nicht eingeschaltet wurde (`ARMOR_ELECTRICAL_SWITCHING=1`) und die ACL des Brokers ihn nicht erlaubt.
* **Wo man es sieht:** der *Elektroplaner* von ARMOR-STUDIO zeichnet das Netz des Hauses und zeigt an jedem Element, was sein Kanal misst; ARMOR-SERVER speichert die Messwerte, ihren Verlauf und die Summen.
* **Die Firmware des Knotens** (ESP32-S3-WROOM-1 N16R8 über WLAN oder das Waveshare ESP32-S3-ETH am Kabel): die Einstellungen, eine serielle Leitung für bis zu sechzehn Zähler und das Web-Panel der anderen Knoten (Einrichtung, Benutzer, WLAN, Broker, Update über die Luft, HTTPS) mit eigenen Seiten für Zähler und Messwerte, in acht Sprachen. Sie liest nur: die Regeln fürs Schalten sind nicht eingebunden. Siehe [die Firmware](docs/NODE_FIRMWARE.md).
* **Konfiguration vom Handy über Bluetooth,** derselbe Kanal wie beim Radarknoten: Die ARMOR-App findet den Knoten als `ARMOR-XXXXXX` und stellt Name, WLAN, Adresse, Broker und Bluetooth-Modus mit den Benutzern und dem Einrichtungscode des Panels ein ([das Protokoll](docs/BLE_PROVISIONING.md)). Er lauscht nur, solange der Knoten keinen Benutzer hat, sofern nichts anderes eingestellt ist. Der Funkteil lief noch nie auf einer Platine.
* **Signierte Over-the-Air-Updates:** der Knoten installiert von GitHub nur ein Abbild, dessen SHA-256 stimmt und das eine gültige Ed25519-Signatur des Betreuers trägt (der öffentliche Schlüssel steckt in der Firmware, der private verlässt den Betreuer nie); eine von einem Administrator im Panel von Hand hochgeladene Datei wird weiterhin akzeptiert. Seiten und Meldungen des Panels sind in acht Sprachen.
* **Bluetooth-Kopplung mit PIN:** die sechsstellige PIN wird aus dem Einrichtungscode abgeleitet, auf der USB-Konsole ausgegeben und einem Administrator im Panel angezeigt; das Telefon muss sie eingeben, bevor es die Einstellungen des Knotens schreiben darf.
* **Noch nicht:** die Firmware auf einer Platine, die Hardware, jede Messung einer echten Anlage und jedes Schalten.
* **Nur das Haus darf das Panel nutzen:** Das Panel antwortet standardmäßig dem privaten Netz (oder den Netzen, die der Besitzer nennt, etwa einem VPN), lehnt eine Liste ab, die den Speichernden aussperren würde, zählt falsche Passwörter je Person und je Adresse und funktioniert hinter dem Proxy des Servers, sodass man es von außerhalb des Hauses erreicht, ohne den Knoten zu veröffentlichen.**Schätzungen sagen es:** ein aus einem Stromeingang gebildeter Kanal trägt `power_estimated`; Studio und Server zeigen seine Leistung als Schätzung (Scheinleistung aus Strom und Nennspannung) und kennzeichnen einen Netzwert, der eine enthält.
* **Optionale verschlüsselte Verbindung zum Broker (TLS):** standardmäßig aus, der Knoten spricht also wie bisher unverschlüsseltes MQTT. Ein Schalter und ein Feld für das Zertifikat der eigenen Zertifizierungsstelle der Anlage (PEM, höchstens 2 KB) auf der Broker-Seite sorgen dafür, dass er sich mit `mqtts://` verbindet, nur dieser Stelle vertraut und prüft, ob das Zertifikat des Brokers für die erreichte Adresse ausgestellt wurde; alles, was nicht genau ein PEM-Zertifikat ist, wird abgelehnt. Durch Host-Tests abgedeckt; der Handshake wurde auf einer Platine noch nicht ausgeführt.
* **Zustandsbericht:** etwa einmal pro Minute veröffentlicht der Knoten, wie lange er läuft und warum er zuletzt neu gestartet ist, seinen freien Speicher, seine Verbindung und sein WLAN-Signal, wie oft er sich mit dem Broker verbunden hat und den letzten protokollierten Fehler (`armor/health/<Knoten>`, der Vertrag `node_health` von ARMOR-COMMON). Der Code, der ihn erzeugt, wird von allen Knoten geteilt und auf einem Computer getestet; auf einer Platine ist er noch nicht gelaufen.
* **Steuerung vom Server aus:** Studio kann den Knoten sauber neu starten, bis zu einer Stunde pausieren (er läuft von selbst weiter), seine Arbeit in den Standby setzen (ohne Ende, über Neustarts hinweg gespeichert, der Knoten bleibt verbunden und antwortet weiter), ihn sich zeigen lassen, einen Diagnose-Schnappschuss anfordern und ihn als in Wartung markieren (`armor/control/<Knoten>/...`, der Vertrag `node_control` von ARMOR-COMMON). Jeder Befehl besteht aus zwei Schritten mit einem Einmal-Token, das zu dieser Operation gehört; nur der Server darf sie am Broker schreiben, und es gibt kein Zurücksetzen auf Werkseinstellungen, kein Löschen und keinen Tiefschlaf. Solange der Knoten pausiert oder im Standby ist, sind alle Relais offen und keines bewegt sich, wer auch fragt (Panel, Broker oder eine Zentrale); die Zähler werden weiter gelesen. Der Code wird von allen Knoten geteilt und auf einem Rechner getestet; auf einer Platine ist er noch nicht gelaufen.
* **Messwerte, die ihre Frische angeben:** Jeder Zählerkanal trägt das Alter seiner Messung und die Zahl der fehlgeschlagenen Anfragen an den Zähler, sodass der Server ein echtes 0 W von einem Zähler unterscheidet, der nicht mehr antwortet; und eine hängende Zähler-Task startet die Platine über den Watchdog neu.
* **Diagnose ohne Kabel:** Nach einem unsauberen Start (Absturz, Watchdog, Spannungseinbruch, fehlgeschlagene Speicherreservierung) nennt der Gesundheitsbericht zehn Minuten lang den Grund; ein Absturz hinterlässt sein Core-Dump, das man im Panel ansehen, herunterladen oder löschen kann; die gespeicherten Einstellungen werden vor jeder Firmware-Installation kopiert und wiederhergestellt, falls ein Update sie beschädigt; und ein Administrator kann den Knoten über den Server mit einem Einmal-Token nach einem Diagnosepaket oder seinem Protokoll fragen.
* **Relais-Verriegelung:** Ein Relais kann ein anderes benennen, das nie gleichzeitig geschlossen sein darf (die zwei Quellen einer Last, die zwei Richtungen eines Motors); das Schließen des einen wird abgelehnt, solange das andere geschlossen ist, vom Panel, vom Broker und von der Zentrale gleichermaßen, und der Knoten öffnet das andere nie von selbst. Auf einem Computer getestet; nicht auf einem Board ausgeführt.

## 🖼️ Screenshots

Aufgenommen mit 1920×1080 auf einem Prüfstand, mit Beispieldaten und ohne eine echte Anlage dahinter.

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="Übersicht: der Zustand des Knotens auf einen Blick - Netzwerk, Broker, Firmware und was er meldet." width="100%"><br><sub>Übersicht: der Zustand des Knotens auf einen Blick - Netzwerk, Broker, Firmware und was er meldet.</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="Zähler: die Energiezähler und Analogkanäle, die der Knoten liest, jeweils mit Name und Skala." width="100%"><br><sub>Zähler: die Energiezähler und Analogkanäle, die der Knoten liest, jeweils mit Name und Skala.</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="Messwerte: was der Knoten gerade liest, mit dem Alter jedes Wertes." width="100%"><br><sub>Messwerte: was der Knoten gerade liest, mit dem Alter jedes Wertes.</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Zigbee-Funk: die serielle Brücke zum Koordinatormodul, welcher Client sie nutzen darf, und der Zustand der Brücke." width="100%"><br><sub>Zigbee-Funk: die serielle Brücke zum Koordinatormodul, welcher Client sie nutzen darf, und der Zustand der Brücke.</sub></td></tr>
</table>

## 🧩 Rolle in A.R.M.O.R.

ARMOR-NODE-ELECTRICAL misst das AC- und DC-Netz des Hauses mit seinen Zählern und hält die Regeln zum Umschalten zwischen Quellen. Es veröffentlicht die Messwerte für [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER), der Verlauf und Tagesenergie führt und die Alarme auslöst; das Menü *Elektro* und der *Elektro-Designer* von [ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) sowie der Elektro-Bildschirm von [ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) zeigen es. Das Schalten ist entworfen (Scharfstellen, ein Einmal-Token, die Verriegelung, die Bestätigung beider Schütze), aber nicht aktiviert.

## 📂 Struktur des Repositorys

```text
ARMOR-NODE-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (8 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_meters, test_runner, test_config, test_mqtt_tls, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ Entwicklungsumgebung

**Voraussetzungen:** ein C++17-Compiler für die Host-Tests, der ESP-IDF-5.5.5-Container zum Bauen des Images, ein ESP32-S3-WROOM-1 N16R8 oder ein Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 380,189 checks: the meters (69), the loop that asks them (81), the settings (93), the readings and inputs (479), Bluetooth (68), the shared base (2,179), the control orders and diagnostics (1,799), the limits (260), the central link (19), the encrypted link (45), the release and signature checks (61), the interlock (135,736) and the switch sets (239,300)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Siehe den [Entwurf](docs/DESIGN.md), die [Sicherheitshinweise](docs/SAFETY.md), die [Regeln fürs Schalten](docs/SWITCHING.md), die [Protokolle](docs/PROTOCOLS.md) und die [Nachrichten](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Verwandte Projekte

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) ist ein Perimeter-Sicherheitssystem aus unabhängigen Repositorys. Jedes hat eine eigene Version, eigene Tests und ein eigenes README; hier ist die Familie:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Nachrichtenverträge, Validierer, Konformitätsvektoren und generierte Typen
* **[ARMOR-NODE-RADAR](https://github.com/JuanenRac/ARMOR-NODE-RADAR)** - Feldknoten-Firmware für ESP32-S3 mit drei Radaren und eigenem Web-Panel
* **[ARMOR-NODE-SOLAR](https://github.com/JuanenRac/ARMOR-NODE-SOLAR)** - Protokolle für Solar-Wechselrichter und -Batterien und die Nachrichten eines Gateway-Knotens
* **ARMOR-NODE-ELECTRICAL** (dieses Repository) - Elektroknoten: Zähler, die Nachricht der Netzmesswerte und die Regeln fürs Schalten
* **[ARMOR-NODE-ALARM](https://github.com/JuanenRac/ARMOR-NODE-ALARM)** - Alarmknoten und Alarmzentrale: Zonen, Scharfschalten, Verzögerungen, Sirene und PIN, mit dem Server oder ohne ihn
* **[ARMOR-NODE-HMI](https://github.com/JuanenRac/ARMOR-NODE-HMI)** - Touch-Panel: der Systemzustand auf einem Wandbildschirm, Scharf- und Quittieren sowie das Zuhause des Sprachassistenten
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - Das lokale Netzwerk: seine Geräte, das Internet und was sich ändert
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Zentraler Koordinator: Telemetrie, Alarme, Geräte, Solarmesswerte und Kameras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Web-Konsole: Kameras, Radar, Alarme, Solarenergie und 2D/3D-Standortdesigner
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Android-Bedienclient mit Live-Radar in 2D/3D
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Visuelle Inferenzrichtlinie, die ihre Entscheidungen erklärt und nie handelt
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Kamerazonen, Masken und Erkennungsregeln für den Jetson Orin NX
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Offline-Sprachabsichten mit einer nicht fälschbaren Bestätigung
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Gehäuse, Elektronik und die Abnahmematrix am Prüfstand
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Bereitstellung, CM5-Prüfstand, Backup und TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Offline-Telemetriesimulator mit wiederholbaren Fehlern
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Erkennt, installiert und aktualisiert die eigenen Repositories des Ökosystems
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architektur, Sicherheitsgrundlage und die Fähigkeitsmatrix

## 📚 Dokumentation und Community

Hier gibt es mehr zu lesen:

* [Fähigkeitsmatrix: was belegt ist und was nicht](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Projektkatalog: Versionen und wie die Repositorys voneinander abhängen](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Änderungsverlauf dieses Repositorys](CHANGELOG.md)
* [Lizenz (GPL-3.0-or-later)](LICENSE)
* Fragen, Ideen und Meldungen: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LIZENZ

GPL-3.0-or-later - siehe [LICENSE](LICENSE).
