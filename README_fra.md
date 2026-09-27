<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  🇫🇷 <b>Français</b> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nœud électrique : lit les tensions, courants, puissance et énergie du réseau AC et DC de la maison et détient les règles pour le commander (cœur testé sur ordinateur et un firmware qui compile ; il n'a jamais tourné sur une carte)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-135%2C990-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-ff9800.svg" alt="Maturity">
</p>

---

**Vérification d'honnêteté - ce qui fonctionne aujourd'hui:** **Maturité : scaffolding.** Le cœur (les trames des compteurs PZEM-004T v3 et PZEM-017, le bus qui les interroge, le message du nœud et les règles de commutation : 53 contrôles des compteurs et 135 736 des règles de commutation) est testé sur ordinateur avec des compteurs et des contacteurs simulés, et les messages qu'il produit sont acceptés par ARMOR-COMMON. Le firmware du nœud (les réglages, la ligne série et les pages Compteurs et Relevés de son panneau : 71 contrôles de plus) compile dans le conteneur ESP-IDF 5.4.2 pour les deux cartes. **Il n'a jamais tourné sur une carte, rien n'a été branché à un compteur ni à un contacteur, et rien ici ne commande quoi que ce soit.**

---

## 🎯 Présentation

* **Lecture des compteurs :** les trames Modbus RTU des **PZEM-004T v3** (AC) et **PZEM-017** (DC) de Peacefair : le CRC, la requête qui lit les registres et le décodage de la réponse (tension, courant, puissance, énergie, fréquence, facteur de puissance, alarme). Seule la requête de lecture est construite : celles qui écrivent dans un compteur (son adresse, ses seuils, la remise à zéro de son énergie) ne le sont nulle part. Une réponse qui ne convient pas, ou qui contient une valeur qu'aucun compteur réel ne pourrait donner, est refusée.
* **Le bus :** une ligne avec plusieurs compteurs est interrogée un par un, avec un délai ; la dernière bonne lecture de chacun est gardée et un compteur qui se tait n'est plus publié au bout de dix secondes.
* **Le message** `armor/electrical/<nœud>/state` : une entrée par canal (un circuit, une ligne, l'entrée réseau, un bus DC) avec AC ou DC, tension, courant, puissance, énergie, fréquence, facteur de puissance, l'état d'un interrupteur tel que le nœud le voit et une alarme ; il est dans le contrat partagé et porte un état, jamais un ordre.
* **Les règles de commutation,** indépendantes de tout matériel : un contrôleur du transfert entre deux sources qui ne commande jamais les deux, exige la commutation autorisée, le nœud armé juste avant et les deux contacteurs confirmés ouverts pendant tout le temps mort, et transforme un contacteur qui ne montre pas ce qu'on lui a demandé en défaut qui reste jusqu'à son acquittement. **La commutation est désactivée par défaut et rien ne pilote de matériel.**
* **Les ordres à un commutateur, non activés :** le message d'état peut porter l'état des commutateurs du nœud, et le contrat partagé a un ordre (`arm`, puis `close_a` ou `close_b` avec un jeton à usage unique, `open`, `acknowledge`) et la réponse du nœud ; le cœur les lit et y répond avec les règles ci-dessus, testé avec les vecteurs partagés et des contacteurs simulés. Aucune image du firmware ne l'inclut, donc rien ne peut piloter un contacteur, et ARMOR-SERVER refuse d'envoyer un ordre tant qu'il n'a pas été activé (`ARMOR_ELECTRICAL_SWITCHING=1`) et que l'ACL du broker ne l'autorise pas.
* **Où on le voit :** le *Concepteur électrique* d'ARMOR-STUDIO dessine le réseau de la maison et montre sur chaque élément ce que mesure son canal ; ARMOR-SERVER conserve les mesures, leur historique et les sommes.
* **Le firmware du nœud** (ESP32-S3-WROOM-1 N16R8 en Wi-Fi, ou la Waveshare ESP32-S3-ETH sur son câble) : les réglages, une ligne série pour jusqu'à seize compteurs et le panneau web des autres nœuds (mise en service, utilisateurs, Wi-Fi, broker, mise à jour par voie hertzienne, HTTPS) avec ses propres pages Compteurs et Relevés, en sept langues. Il ne fait que lire : les règles de commutation n'y sont pas liées. Voir [le firmware](docs/NODE_FIRMWARE.md).
* **Configuration depuis le téléphone en Bluetooth,** le même canal que celui du nœud radar : l'app ARMOR trouve le nœud sous le nom `ARMOR-XXXXXX` et règle son nom, son Wi-Fi, son adresse, son broker et son mode Bluetooth avec les utilisateurs et le code de mise en service du panneau ([le protocole](docs/BLE_PROVISIONING.md)). Il n'écoute que tant que le nœud n'a pas d'utilisateur, sauf réglage contraire. La partie radio n'a jamais tourné sur une carte.
* **Pas encore :** le firmware qui tourne sur une carte, le matériel, toute mesure d'une installation réelle et toute commutation.

## 📂 Structure du dépôt

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

## 🛠️ Environnement de développement

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 135,990 checks: the meters (53), the loop that asks them (62), the settings (71), Bluetooth (68), the switching rules (135,736)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Voir la [conception](docs/DESIGN.md), les [notes de sécurité](docs/SAFETY.md), les [règles de commutation](docs/SWITCHING.md), les [protocoles](docs/PROTOCOLS.md) et les [messages](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Projets liés

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) est un système de sécurité périmétrique composé de dépôts indépendants. Chacun a sa propre version, ses propres tests et son propre README ; voici la famille :

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contrats de messages, validateurs, vecteurs de conformité et types générés
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Firmware du nœud de terrain pour ESP32-S3 avec trois radars et son propre panneau web
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - Protocoles des onduleurs et batteries solaires et messages d'un nœud passerelle
* **ARMOR-ELECTRICAL** (ce dépôt) - Nœud électrique : compteurs, le message des mesures du réseau et les règles de commutation
* **[ARMOR-NETWORK](../ARMOR-NETWORK)** - Le réseau local : ses appareils, internet et ce qui change
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinateur central : télémétrie, alarmes, appareils, relevés solaires et caméras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Console web : caméras, radar, alarmes, énergie solaire et concepteur de site 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Client Android de l'opérateur avec radar 2D/3D en direct
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Politique d'inférence visuelle qui explique ses décisions et n'agit jamais
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intentions vocales hors ligne avec une confirmation impossible à falsifier
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Boîtiers, électronique et matrice d'acceptation sur banc
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Déploiement, banc d'essai CM5, sauvegarde et TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulateur de télémétrie hors ligne avec des pannes reproductibles
* **[ARMOR-UPDATER](../ARMOR-UPDATER)** - Détecte, installe et met à jour les propres dépôts de l'écosystème
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architecture, base de sécurité et matrice des capacités

## 📚 Documentation et communauté

Pour en savoir plus :

* [Matrice des capacités : ce qui est prouvé et ce qui ne l'est pas](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catalogue des projets : versions et dépendances entre les dépôts](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Historique des modifications de ce dépôt](CHANGELOG.md)
* [Licence (GPL-3.0-or-later)](LICENSE)
* Questions, idées et rapports : electrohobby3d@gmail.com

## 👤 AUTEUR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCE

GPL-3.0-or-later - voir [LICENSE](LICENSE).
