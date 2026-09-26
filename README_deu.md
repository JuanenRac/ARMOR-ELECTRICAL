<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Elektroknoten: liest Spannungen, Ströme, Leistung und Energie des AC- und DC-Netzes des Hauses und hält die Regeln fürs Schalten (am Rechner getesteter Kern und eine Firmware, die baut; sie lief noch nie auf einer Platine)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C789-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**Ehrlichkeitsprüfung - was heute läuft:** **Reifegrad: Scaffolding.** Der Kern (die Rahmen der Zähler PZEM-004T v3 und PZEM-017, der Bus, der sie abfragt, die Nachricht des Knotens und die Regeln fürs Schalten: 53 Prüfungen der Zähler und 135.736 der Schaltregeln) wird auf einem Rechner mit nachgebildeten Zählern und Schützen getestet, und die Nachrichten, die er erzeugt, akzeptiert ARMOR-COMMON. Die Firmware des Knotens (die Einstellungen, die serielle Leitung und die Seiten Zähler und Messwerte ihres Panels: 71 weitere Prüfungen) baut im ESP-IDF-5.4.2-Container für beide Platinen. **Sie lief noch nie auf einer Platine, nichts wurde an einen Zähler oder ein Schütz angeschlossen, und hier schaltet nichts irgendetwas.**

---

## 🎯 Überblick

* **Zähler lesen:** die Modbus-RTU-Rahmen der Peacefair-Zähler **PZEM-004T v3** (AC) und **PZEM-017** (DC): die CRC, die Anfrage, die die Register liest, und die Auswertung der Antwort (Spannung, Strom, Leistung, Energie, Frequenz, Leistungsfaktor, Alarm). Nur die Leseanfrage wird gebaut: die schreibenden (Adresse, Schwellen, Zurücksetzen der Energie) werden nirgends gebaut. Eine Antwort, die nicht passt oder einen Wert enthält, den kein echter Zähler liefern könnte, wird abgelehnt.
* **Der Bus:** eine Leitung mit mehreren Zählern wird einzeln abgefragt, mit Zeitlimit; der letzte gute Wert jedes Zählers wird behalten, und ein Zähler, der schweigt, wird nach zehn Sekunden nicht mehr veröffentlicht.
* **Die Nachricht** `armor/electrical/<Knoten>/state`: ein Eintrag je Kanal (ein Stromkreis, eine Leitung, der Netzanschluss, ein DC-Bus) mit AC oder DC, Spannung, Strom, Leistung, Energie, Frequenz, Leistungsfaktor, dem Zustand eines Schalters, wie ihn der Knoten sieht, und einem Alarm; sie steht im gemeinsamen Vertrag und trägt einen Zustand, nie einen Befehl.
* **Die Regeln fürs Schalten,** unabhängig von jeder Hardware: ein Regler für den Wechsel zwischen zwei Quellen, der nie beide ansteuert, das erlaubte Schalten, den kurz zuvor scharfgeschalteten Knoten und beide Schütze über die ganze Totzeit als offen bestätigt verlangt und ein Schütz, das nicht zeigt, was ihm befohlen wurde, zu einer Störung macht, die bis zur Quittierung bleibt. **Schalten ist standardmäßig aus, und nichts steuert irgendeine Hardware.**
* **Wo man es sieht:** der *Elektroplaner* von ARMOR-STUDIO zeichnet das Netz des Hauses und zeigt an jedem Element, was sein Kanal misst; ARMOR-SERVER speichert die Messwerte, ihren Verlauf und die Summen.
* **Die Firmware des Knotens** (ESP32-S3-WROOM-1 N16R8 über WLAN oder das Waveshare ESP32-S3-ETH am Kabel): die Einstellungen, eine serielle Leitung für bis zu sechzehn Zähler und das Web-Panel der anderen Knoten (Einrichtung, Benutzer, WLAN, Broker, Update über die Luft, HTTPS) mit eigenen Seiten für Zähler und Messwerte, in sieben Sprachen. Sie liest nur: die Regeln fürs Schalten sind nicht eingebunden. Siehe [die Firmware](docs/NODE_FIRMWARE.md).
* **Noch nicht:** die Firmware auf einer Platine, die Hardware, jede Messung einer echten Anlage und jedes Schalten.

## 📂 Struktur des Repositorys

```text
ARMOR-ELECTRICAL/
├── core/    pzem.hpp (frames of the PZEM meters), pzem_bus.hpp (the line), electrical_json.hpp (the message), interlock.hpp (the rules for switching), json.hpp
├── tests/   test_meters.cpp, test_interlock.cpp, emit_samples.cpp + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE
└── images/  brand assets
```

## 🛠️ Entwicklungsumgebung

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # the meters (53 checks) and the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
```

Siehe den [Entwurf](docs/DESIGN.md), die [Sicherheitshinweise](docs/SAFETY.md), die [Regeln fürs Schalten](docs/SWITCHING.md), die [Protokolle](docs/PROTOCOLS.md) und die [Nachrichten](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Verwandte Projekte

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) ist ein Perimeter-Sicherheitssystem aus unabhängigen Repositorys. Jedes hat eine eigene Version, eigene Tests und ein eigenes README; hier ist die Familie:

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Nachrichtenverträge, Validierer, Konformitätsvektoren und generierte Typen
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Feldknoten-Firmware für ESP32-S3 mit drei Radaren und eigenem Web-Panel
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - Protokolle für Solar-Wechselrichter und -Batterien und die Nachrichten eines Gateway-Knotens
* **ARMOR-ELECTRICAL** (dieses Repository) - Elektroknoten: Zähler, die Nachricht der Netzmesswerte und die Regeln fürs Schalten
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Zentraler Koordinator: Telemetrie, Alarme, Geräte, Solarmesswerte und Kameras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Web-Konsole: Kameras, Radar, Alarme, Solarenergie und 2D/3D-Standortdesigner
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Android-Bedienclient mit Live-Radar in 2D/3D
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Visuelle Inferenzrichtlinie, die ihre Entscheidungen erklärt und nie handelt
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Offline-Sprachabsichten mit einer nicht fälschbaren Bestätigung
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Gehäuse, Elektronik und die Abnahmematrix am Prüfstand
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Bereitstellung, CM5-Prüfstand, Backup und TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Offline-Telemetriesimulator mit wiederholbaren Fehlern
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architektur, Sicherheitsgrundlage und die Fähigkeitsmatrix

## 📚 Dokumentation und Community

Hier gibt es mehr zu lesen:

* [Fähigkeitsmatrix: was belegt ist und was nicht](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Projektkatalog: Versionen und wie die Repositorys voneinander abhängen](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Änderungsverlauf dieses Repositorys](CHANGELOG.md)
* [Lizenz (GPL-3.0-or-later)](LICENSE)
* Fragen, Ideen und Meldungen: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LIZENZ

GPL-3.0-or-later - siehe [LICENSE](LICENSE).
