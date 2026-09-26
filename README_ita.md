<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  🇮🇹 <b>Italiano</b> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nodo elettrico: legge tensioni, correnti, potenza ed energia della rete AC e DC della casa e custodisce le regole per manovrarla (nucleo testato sul PC e un firmware che compila; non ha mai girato su una scheda)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C990-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**Controllo di onestà - cosa funziona oggi:** **Maturità: scaffolding.** Il nucleo (i frame dei contatori PZEM-004T v3 e PZEM-017, il bus che li interroga, il messaggio del nodo e le regole di manovra: 53 controlli dei contatori e 135.736 delle regole di manovra) è testato su un computer con contatori e contattori simulati, e i messaggi che produce sono accettati da ARMOR-COMMON. Il firmware del nodo (le impostazioni, la linea seriale e le pagine Contatori e Letture del suo pannello: 71 controlli in più) compila nel container ESP-IDF 5.4.2 per le due schede. **Non ha mai girato su una scheda, nulla è stato collegato a un contatore o a un contattore, e nulla qui comanda niente.**

---

## 🎯 Panoramica

* **Lettura dei contatori:** i frame Modbus RTU dei **PZEM-004T v3** (AC) e **PZEM-017** (DC) di Peacefair: il CRC, la richiesta che legge i registri e la decodifica della risposta (tensione, corrente, potenza, energia, frequenza, fattore di potenza, allarme). Si costruisce solo la richiesta di lettura: quelle che scrivono in un contatore (indirizzo, soglie, azzeramento dell'energia) non si costruiscono da nessuna parte. Una risposta che non torna, o con un valore che nessun contatore reale potrebbe dare, viene rifiutata.
* **Il bus:** una linea con più contatori si interroga uno alla volta, con un timeout; si conserva l'ultima lettura buona di ciascuno e un contatore che tace non viene più pubblicato dopo dieci secondi.
* **Il messaggio** `armor/electrical/<nodo>/state`: una voce per canale (un circuito, una linea, l'ingresso di rete, un bus DC) con AC o DC, tensione, corrente, potenza, energia, frequenza, fattore di potenza, lo stato di un interruttore come lo vede il nodo e un allarme; è nel contratto condiviso e porta uno stato, mai un comando.
* **Le regole di manovra,** a prescindere da qualsiasi hardware: un controllore dello scambio tra due sorgenti che non comanda mai entrambe, richiede la manovra consentita, il nodo armato poco prima e i due contattori confermati aperti per tutto il tempo morto, e trasforma un contattore che non mostra ciò che gli è stato ordinato in un guasto che resta finché non viene riconosciuto. **La manovra è disattivata per impostazione predefinita e nulla pilota alcun hardware.**
* **Dove si vede:** il *Progettista elettrico* di ARMOR-STUDIO disegna la rete della casa e mostra su ogni elemento ciò che misura il suo canale; ARMOR-SERVER conserva le letture, la loro cronologia e le somme.
* **Il firmware del nodo** (ESP32-S3-WROOM-1 N16R8 in Wi-Fi, o la Waveshare ESP32-S3-ETH sul suo cavo): le impostazioni, una linea seriale per fino a sedici contatori e il pannello web degli altri nodi (messa in servizio, utenti, Wi-Fi, broker, aggiornamento via etere, HTTPS) con le sue pagine Contatori e Letture, in sette lingue. Legge soltanto: le regole di manovra non sono collegate. Vedi [il firmware](docs/NODE_FIRMWARE.md).
* **Configurazione dal telefono via Bluetooth,** lo stesso canale del nodo radar: l'app ARMOR trova il nodo come `ARMOR-XXXXXX` e ne imposta nome, Wi-Fi, indirizzo, broker e modo Bluetooth con gli utenti e il codice di messa in servizio del pannello ([il protocollo](docs/BLE_PROVISIONING.md)). Ascolta solo finché il nodo non ha utenti, salvo diversa impostazione. La parte radio non ha mai girato su una scheda.
* **Non ancora:** il firmware in funzione su una scheda, l'hardware, qualsiasi misura di un impianto reale e qualsiasi manovra.

## 📂 Struttura del repository

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

## 🛠️ Ambiente di sviluppo

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 135,990 checks: the meters (53), the loop that asks them (62), the settings (71), Bluetooth (68), the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Vedi il [progetto](docs/DESIGN.md), le [note di sicurezza](docs/SAFETY.md), le [regole di manovra](docs/SWITCHING.md), i [protocolli](docs/PROTOCOLS.md) e i [messaggi](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Progetti correlati

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) è un sistema di sicurezza perimetrale fatto di repository indipendenti. Ognuno ha la propria versione, i propri test e il proprio README; ecco la famiglia:

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contratti dei messaggi, validatori, vettori di conformità e tipi generati
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Firmware del nodo di campo per ESP32-S3 con tre radar e un proprio pannello web
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - Protocolli di inverter e batterie solari e messaggi di un nodo gateway
* **ARMOR-ELECTRICAL** (questo repository) - Nodo elettrico: contatori, il messaggio delle letture della rete e le regole di manovra
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinatore centrale: telemetria, allarmi, dispositivi, letture solari e telecamere
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Console web: telecamere, radar, allarmi, energia solare e progettista del sito 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Client Android dell'operatore con radar 2D/3D in tempo reale
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Politica di inferenza visiva che spiega le sue decisioni e non agisce mai
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intenti vocali offline con una conferma impossibile da falsificare
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Contenitori, elettronica e matrice di accettazione da banco
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Distribuzione, banco di prova CM5, backup e TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulatore di telemetria offline con guasti ripetibili
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architettura, base di sicurezza e matrice delle capacità

## 📚 Documentazione e comunità

Dove leggere di più:

* [Matrice delle capacità: cosa è provato e cosa no](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catalogo dei progetti: versioni e dipendenze tra i repository](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Cronologia delle modifiche di questo repository](CHANGELOG.md)
* [Licenza (GPL-3.0-or-later)](LICENSE)
* Domande, idee e segnalazioni: electrohobby3d@gmail.com

## 👤 AUTORE

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENZA

GPL-3.0-or-later - vedi [LICENSE](LICENSE).
