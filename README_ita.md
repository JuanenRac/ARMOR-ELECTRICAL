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
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### Nodo elettrico: legge tensioni, correnti, potenza ed energia della rete AC e DC della casa e custodisce le regole per manovrarla (nucleo testato sul PC e un firmware che compila; non ha mai girato su una scheda)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.4.5-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**Controllo di onestà - cosa funziona oggi:** Il nucleo (i frame dei contatori PZEM-004T v3 e PZEM-017, il bus che li interroga, il messaggio del nodo, gli ingressi analogici e le regole di commutazione: 376.195 controlli in 11 programmi di test, 375.008 dei quali sulle regole di commutazione) è provato su un computer con contatori e contattori sostitutivi, e i messaggi che produce sono accettati da ARMOR-COMMON. Il firmware del nodo (le impostazioni, la linea seriale, le pagine Contatori e Letture del suo pannello, Bluetooth, aggiornamenti firmati) si compila nel contenitore ESP-IDF 5.4 per entrambe le schede. **Non ha mai girato su una scheda, nulla è stato collegato a un contatore o a un contattore, e nulla qui commuta niente.**

---

## 📋 In breve

| | |
|---|---|
| Versione | `0.4.5` |
| Maturità | Funzionale |
| Test | 379.655 controlli in 16 programmi (`ctest`) |
| Tecnologia | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| Su hardware reale | Ancora nessuno: nessuna scheda montata né contatore o contattore collegati; nulla commuta niente |

## 🎯 Panoramica

* **Lettura dei contatori:** i frame Modbus RTU dei **PZEM-004T v3** (AC) e **PZEM-017** (DC) di Peacefair: il CRC, la richiesta che legge i registri e la decodifica della risposta (tensione, corrente, potenza, energia, frequenza, fattore di potenza, allarme). Si costruisce solo la richiesta di lettura: quelle che scrivono in un contatore (indirizzo, soglie, azzeramento dell'energia) non si costruiscono da nessuna parte. Una risposta che non torna, o con un valore che nessun contatore reale potrebbe dare, viene rifiutata.
* **Il bus:** una linea con più contatori si interroga uno alla volta, con un timeout; si conserva l'ultima lettura buona di ciascuno e un contatore che tace non viene più pubblicato dopo dieci secondi.
* **Il messaggio** `armor/electrical/<nodo>/state`: una voce per canale (un circuito, una linea, l'ingresso di rete, un bus DC) con AC o DC, tensione, corrente, potenza, energia, frequenza, fattore di potenza, lo stato di un interruttore come lo vede il nodo e un allarme; è nel contratto condiviso e porta uno stato, mai un comando.
* **Le regole di manovra,** a prescindere da qualsiasi hardware: un controllore dello scambio tra due sorgenti che non comanda mai entrambe, richiede la manovra consentita, il nodo armato poco prima e i due contattori confermati aperti per tutto il tempo morto, e trasforma un contattore che non mostra ciò che gli è stato ordinato in un guasto che resta finché non viene riconosciuto. **La manovra è disattivata per impostazione predefinita e nulla pilota alcun hardware.**
* **I comandi a un commutatore, non attivati:** il messaggio di stato può portare lo stato dei commutatori del nodo, e il contratto condiviso ha un comando (`arm`, poi `close_a` o `close_b` con un token monouso, `open`, `acknowledge`) e la risposta del nodo; il nucleo li legge e risponde con le regole sopra, provato con i vettori condivisi e con contattori simulati. Nessuna immagine del firmware lo include, quindi nulla può azionare un contattore, e ARMOR-SERVER rifiuta di inviare un comando se non è stato attivato (`ARMOR_ELECTRICAL_SWITCHING=1`) e l'ACL del broker non lo consente.
* **Dove si vede:** il *Progettista elettrico* di ARMOR-STUDIO disegna la rete della casa e mostra su ogni elemento ciò che misura il suo canale; ARMOR-SERVER conserva le letture, la loro cronologia e le somme.
* **Il firmware del nodo** (ESP32-S3-WROOM-1 N16R8 in Wi-Fi, o la Waveshare ESP32-S3-ETH sul suo cavo): le impostazioni, una linea seriale per fino a sedici contatori e il pannello web degli altri nodi (messa in servizio, utenti, Wi-Fi, broker, aggiornamento via etere, HTTPS) con le sue pagine Contatori e Letture, in otto lingue. Legge soltanto: le regole di manovra non sono collegate. Vedi [il firmware](docs/NODE_FIRMWARE.md).
* **Configurazione dal telefono via Bluetooth,** lo stesso canale del nodo radar: l'app ARMOR trova il nodo come `ARMOR-XXXXXX` e ne imposta nome, Wi-Fi, indirizzo, broker e modo Bluetooth con gli utenti e il codice di messa in servizio del pannello ([il protocollo](docs/BLE_PROVISIONING.md)). Ascolta solo finché il nodo non ha utenti, salvo diversa impostazione. La parte radio non ha mai girato su una scheda.
* **Aggiornamenti via etere firmati:** il nodo installa da GitHub solo un'immagine il cui SHA-256 coincide e che porta una firma Ed25519 valida del responsabile (la chiave pubblica è nel firmware, quella privata non lascia mai il responsabile); un file caricato a mano da un amministratore nel pannello resta accettato. Le pagine e i messaggi del pannello sono in otto lingue.
* **Abbinamento Bluetooth con PIN:** il PIN a sei cifre deriva dal codice di configurazione, è stampato sulla console USB e mostrato a un amministratore nel pannello; il telefono deve inserirlo prima di poter scrivere le impostazioni del nodo.
* **Non ancora:** il firmware in funzione su una scheda, l'hardware, qualsiasi misura di un impianto reale e qualsiasi manovra.
* **Solo la casa può usare il pannello:** il pannello risponde per impostazione predefinita alla rete privata (o alle reti indicate dal proprietario, come una VPN), rifiuta un elenco che escluderebbe chi lo salva, conta le password errate per persona e per indirizzo e funziona dietro il proxy del server per essere raggiunto da fuori casa senza pubblicare il nodo.**Le stime lo dicono:** un canale ricavato da un ingresso di corrente porta `power_estimated`; Studio e il server mostrano la sua potenza come stima (potenza apparente da una corrente e una tensione nominale) e segnalano un valore di rete che ne include una.
* **Collegamento cifrato facoltativo con il broker (TLS):** disattivato per impostazione predefinita, quindi il nodo parla MQTT non cifrato come prima. Un interruttore e una casella per il certificato dell'autorità propria dell'installazione (PEM, al massimo 2 KB) nella pagina Broker lo fanno collegare con `mqtts://`, fidarsi solo di quell'autorità e verificare che il certificato del broker sia stato fatto per l'indirizzo raggiunto; ciò che non è un solo certificato PEM viene rifiutato. Coperto da test sul computer; l'handshake non è ancora stato eseguito su una scheda.
* **Rapporto di salute:** circa una volta al minuto il nodo pubblica da quanto è acceso e perché si è riavviato l'ultima volta, la memoria libera, il collegamento e il segnale Wi-Fi, quante volte si è connesso al broker e l'ultimo errore registrato (`armor/health/<nodo>`, il contratto `node_health` di ARMOR-COMMON). Il codice che lo costruisce è condiviso da tutti i nodi e testato su un computer; non è mai girato su una scheda.
* **Controllo dal server:** Studio può riavviare il nodo in modo pulito, metterlo in pausa fino a un'ora (riprende da solo), mettere il suo lavoro in standby (senza fine, conservato ai riavvii, il nodo resta connesso e continua a rispondere), farlo mostrare, chiedere un'istantanea diagnostica e segnarlo in manutenzione (`armor/control/<nodo>/...`, il contratto `node_control` di ARMOR-COMMON). Ogni ordine è in due passi con un token monouso legato a quell'operazione, solo il server può scriverli sul broker e non esistono ripristino di fabbrica, cancellazione né sonno profondo. Finché il nodo è in pausa o in standby tutti i relè sono aperti e nessuno si muove, chiunque lo chieda (il pannello, il broker o una centrale); i contatori continuano a essere letti. Il codice è condiviso da tutti i nodi e provato su un computer; non ha ancora girato su una scheda.
* **Letture che dicono quanto sono fresche:** ogni canale del contatore porta l'età della sua lettura e quante richieste al contatore sono fallite, così il server distingue uno 0 W reale da un contatore che ha smesso di rispondere; e un task dei contatori bloccato riavvia la scheda tramite il watchdog.
* **Diagnostica senza cavo:** dopo un avvio non pulito (un blocco, un watchdog, un calo di alimentazione, un'allocazione fallita) il rapporto di salute ne indica la causa per dieci minuti; un arresto anomalo lascia il suo core dump da vedere, scaricare o cancellare dal pannello; le impostazioni salvate vengono copiate prima di ogni installazione del firmware e recuperate se un aggiornamento le danneggia; e un amministratore può chiedere al nodo, tramite il server, un pacchetto di diagnostica o il suo registro con un token monouso.

## 🖼️ Schermate

Catturate a 1920×1080 su un banco di prova, con dati di esempio e senza alcuna installazione reale dietro.

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="Riepilogo: lo stato del nodo in breve - rete, broker, firmware e ciò che riporta." width="100%"><br><sub>Riepilogo: lo stato del nodo in breve - rete, broker, firmware e ciò che riporta.</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="Contatori: i contatori di energia e i canali analogici letti dal nodo, ognuno con nome e scala." width="100%"><br><sub>Contatori: i contatori di energia e i canali analogici letti dal nodo, ognuno con nome e scala.</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="Letture: ciò che il nodo legge in questo momento, con l'età di ogni valore." width="100%"><br><sub>Letture: ciò che il nodo legge in questo momento, con l'età di ogni valore.</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Radio Zigbee: il ponte seriale verso il modulo coordinatore, quale client può usarlo e lo stato del ponte." width="100%"><br><sub>Radio Zigbee: il ponte seriale verso il modulo coordinatore, quale client può usarlo e lo stato del ponte.</sub></td></tr>
</table>

## 🧩 Ruolo in A.R.M.O.R.

ARMOR-ELECTRICAL misura la rete CA e CC della casa con i suoi contatori e conserva le regole per commutare fra sorgenti. Pubblica le letture per [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER), che conserva lo storico e l'energia di ogni giorno e genera gli allarmi; il menu *Elettrico* e il *Progettista elettrico* di [ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) e la schermata elettrica di [ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) lo mostrano. La commutazione è progettata (inserire, un token monouso, l'interblocco, la conferma di entrambi i contattori) ma non attivata.

## 📂 Struttura del repository

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

## 🛠️ Ambiente di sviluppo

**Requisiti:** un compilatore C++17 per i test sul computer, il container ESP-IDF 5.4 per compilare l'immagine, un ESP32-S3-WROOM-1 N16R8 o un Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 379,655 checks: the meters (62), the loop that asks them (72), the settings (85), the readings and inputs (477), Bluetooth (68), the shared base (252), the central link (19), the release and signature checks (20), the interlock (135,736) and the switch sets (239,272)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Vedi il [progetto](docs/DESIGN.md), le [note di sicurezza](docs/SAFETY.md), le [regole di manovra](docs/SWITCHING.md), i [protocolli](docs/PROTOCOLS.md) e i [messaggi](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Progetti correlati

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) è un sistema di sicurezza perimetrale fatto di repository indipendenti. Ognuno ha la propria versione, i propri test e il proprio README; ecco la famiglia:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Contratti dei messaggi, validatori, vettori di conformità e tipi generati
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Firmware del nodo di campo per ESP32-S3 con tre radar e un proprio pannello web
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - Protocolli di inverter e batterie solari e messaggi di un nodo gateway
* **ARMOR-ELECTRICAL** (questo repository) - Nodo elettrico: contatori, il messaggio delle letture della rete e le regole di manovra
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - Nodo e centrale d'allarme: zone, inserimento, ritardi, sirena e PIN, con il server o senza
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Pannello touch: lo stato del sistema su uno schermo a parete, attivare e riconoscere gli allarmi, e la casa dell'assistente vocale
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - La rete locale: i suoi dispositivi, internet e ciò che cambia
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordinatore centrale: telemetria, allarmi, dispositivi, letture solari e telecamere
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Console web: telecamere, radar, allarmi, energia solare e progettista del sito 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Client Android dell'operatore con radar 2D/3D in tempo reale
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Politica di inferenza visiva che spiega le sue decisioni e non agisce mai
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Zone delle telecamere, maschere e regole di rilevamento per la Jetson Orin NX
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Intenti vocali offline con una conferma impossibile da falsificare
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Contenitori, elettronica e matrice di accettazione da banco
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Distribuzione, banco di prova CM5, backup e TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Simulatore di telemetria offline con guasti ripetibili
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Rileva, installa e aggiorna i repository stessi dell'ecosistema
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architettura, base di sicurezza e matrice delle capacità

## 📚 Documentazione e comunità

Dove leggere di più:

* [Matrice delle capacità: cosa è provato e cosa no](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Catalogo dei progetti: versioni e dipendenze tra i repository](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Cronologia delle modifiche di questo repository](CHANGELOG.md)
* [Licenza (GPL-3.0-or-later)](LICENSE)
* Domande, idee e segnalazioni: electrohobby3d@gmail.com

## 👤 AUTORE

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENZA

GPL-3.0-or-later - vedi [LICENSE](LICENSE).
