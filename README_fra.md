<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-NODE-ELECTRICAL banner" width="100%">
</p>

# ⚡ ARMOR-NODE-ELECTRICAL

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  🇫🇷 <b>Français</b> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### Nœud électrique : lit les tensions, courants, puissance et énergie du réseau AC et DC de la maison et détient les règles pour le commander (cœur testé sur ordinateur et un firmware qui compile ; il n'a jamais tourné sur une carte)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Meters-PZEM--004T%20%2F%20017-ffb020.svg" alt="Meters">
  <img src="https://img.shields.io/badge/Checks-376%2C195-2ea44f.svg" alt="Checks">
  <img src="https://img.shields.io/badge/Version-0.4.7-0f6cbd.svg" alt="Version">
  <img src="https://img.shields.io/badge/Maturity-functional-00E5FF.svg" alt="Maturity">
</p>

---

**Vérification d'honnêteté - ce qui fonctionne aujourd'hui:** Le cœur (les trames des compteurs PZEM-004T v3 et PZEM-017, le bus qui les interroge, le message du nœud, les entrées analogiques et les règles de commutation : 376 195 vérifications dans 11 programmes de test, dont 375 008 sur les règles de commutation) est testé sur un ordinateur avec des compteurs et contacteurs factices, et les messages qu'il produit sont acceptés par ARMOR-COMMON. Le firmware du nœud (les réglages, la ligne série, les pages Compteurs et Mesures de son panneau, Bluetooth, mises à jour signées) se compile dans le conteneur ESP-IDF 5.5.5 pour les deux cartes. **Il n'a jamais tourné sur une carte, rien n'a été connecté à un compteur ni à un contacteur, et rien ici ne commute quoi que ce soit.**

---

## 📋 En un coup d'œil

| | |
|---|---|
| Version | `0.4.7` |
| Maturité | Fonctionnel |
| Tests | 380 189 vérifications dans 16 programmes (`ctest`) |
| Technologie | C++17 (host-tested core); ESP-IDF firmware for the ESP32-S3 (Wi-Fi or Ethernet), with its web panel |
| Sur matériel réel | Aucun pour l'instant : aucune carte montée ni compteur ou contacteur connecté ; rien ne commute quoi que ce soit |

## 🚦 État du projet

Chaque ligne est une partie de ce dépôt et le feu dit où elle en est aujourd'hui. Elle est écrite d'après ce que le dépôt peut montrer (tests, compilations, exécutions), pas d'après ce qui est prévu.

🟢 Fait et vérifié · 🟡 Fonctionne, avec des limites connues · 🟠 À moitié fait ou pas encore vérifié · 🔴 Pas fait ou ne fonctionne pas

| Domaine | État | Ce que cela veut dire ici |
|---|---|---|
| Logiciel | 🟢 | Le firmware compile dans le conteneur ESP-IDF 5.5.5 pour les deux cartes (Wi-Fi et Ethernet). |
| Tests | 🟢 | 380 189 vérifications dans 16 programmes d'hôte réussissent (`ctest`), dont 375 036 sur les règles de manœuvre ; les compteurs et contacteurs sont des simulations. |
| Protocoles et données | 🟡 | Les trames et lectures des PZEM-004T v3 et PZEM-017 sont vérifiées par rapport aux trames documentées et à des échantillons enregistrés ; aucun compteur n'a encore répondu. |
| Interface | 🟡 | Les pages des compteurs et des mesures du panneau web ont été essayées dans un navigateur contre un nœud simulé, pas sur une carte. |
| Matériel | 🔴 | Aucune carte construite ni compteur ou contacteur connectés : rien n'a encore tourné sur de vrais équipements. |
| Sécurité | 🟡 | Le code de configuration, la connexion avec une limite de mots de passe erronés, le PIN Bluetooth et les mises à jour signées sont testés sur l'hôte ; il n'y a pas eu de revue indépendante. |
| Mises à jour | 🟠 | Les contrôles de hash et de signature Ed25519 et la logique de retour arrière réussissent sur l'hôte ; une mise à jour par voie hertzienne n'a jamais été exécutée sur une carte. |
| Écriture sur les équipements | 🟠 | Les règles de manœuvre des contacteurs ne sont testées que contre des contacteurs simulés ; rien ici n'a encore commandé quoi que ce soit de réel. |
| Intégration | 🟢 | Les messages du nœud sont validés par rapport aux schémas partagés et aux vecteurs de conformité d'ARMOR-COMMON. |
| Documentation | 🟢 | Huit langues, le guide du firmware, les notes de câblage et le journal des modifications sont à jour. |

## 🎯 Présentation

* **Lecture des compteurs :** les trames Modbus RTU des **PZEM-004T v3** (AC) et **PZEM-017** (DC) de Peacefair : le CRC, la requête qui lit les registres et le décodage de la réponse (tension, courant, puissance, énergie, fréquence, facteur de puissance, alarme). Seule la requête de lecture est construite : celles qui écrivent dans un compteur (son adresse, ses seuils, la remise à zéro de son énergie) ne le sont nulle part. Une réponse qui ne convient pas, ou qui contient une valeur qu'aucun compteur réel ne pourrait donner, est refusée.
* **Le bus :** une ligne avec plusieurs compteurs est interrogée un par un, avec un délai ; la dernière bonne lecture de chacun est gardée et un compteur qui se tait n'est plus publié au bout de dix secondes.
* **Le message** `armor/electrical/<nœud>/state` : une entrée par canal (un circuit, une ligne, l'entrée réseau, un bus DC) avec AC ou DC, tension, courant, puissance, énergie, fréquence, facteur de puissance, l'état d'un interrupteur tel que le nœud le voit et une alarme ; il est dans le contrat partagé et porte un état, jamais un ordre.
* **Les règles de commutation,** indépendantes de tout matériel : un contrôleur du transfert entre deux sources qui ne commande jamais les deux, exige la commutation autorisée, le nœud armé juste avant et les deux contacteurs confirmés ouverts pendant tout le temps mort, et transforme un contacteur qui ne montre pas ce qu'on lui a demandé en défaut qui reste jusqu'à son acquittement. **La commutation est désactivée par défaut et rien ne pilote de matériel.**
* **Les ordres à un commutateur, non activés :** le message d'état peut porter l'état des commutateurs du nœud, et le contrat partagé a un ordre (`arm`, puis `close_a` ou `close_b` avec un jeton à usage unique, `open`, `acknowledge`) et la réponse du nœud ; le cœur les lit et y répond avec les règles ci-dessus, testé avec les vecteurs partagés et des contacteurs simulés. Aucune image du firmware ne l'inclut, donc rien ne peut piloter un contacteur, et ARMOR-SERVER refuse d'envoyer un ordre tant qu'il n'a pas été activé (`ARMOR_ELECTRICAL_SWITCHING=1`) et que l'ACL du broker ne l'autorise pas.
* **Où on le voit :** le *Concepteur électrique* d'ARMOR-STUDIO dessine le réseau de la maison et montre sur chaque élément ce que mesure son canal ; ARMOR-SERVER conserve les mesures, leur historique et les sommes.
* **Le firmware du nœud** (ESP32-S3-WROOM-1 N16R8 en Wi-Fi, ou la Waveshare ESP32-S3-ETH sur son câble) : les réglages, une ligne série pour jusqu'à seize compteurs et le panneau web des autres nœuds (mise en service, utilisateurs, Wi-Fi, broker, mise à jour par voie hertzienne, HTTPS) avec ses propres pages Compteurs et Relevés, en huit langues. Il ne fait que lire : les règles de commutation n'y sont pas liées. Voir [le firmware](docs/NODE_FIRMWARE.md).
* **Configuration depuis le téléphone en Bluetooth,** le même canal que celui du nœud radar : l'app ARMOR trouve le nœud sous le nom `ARMOR-XXXXXX` et règle son nom, son Wi-Fi, son adresse, son broker et son mode Bluetooth avec les utilisateurs et le code de mise en service du panneau ([le protocole](docs/BLE_PROVISIONING.md)). Il n'écoute que tant que le nœud n'a pas d'utilisateur, sauf réglage contraire. La partie radio n'a jamais tourné sur une carte.
* **Mises à jour par radio signées :** le nœud n'installe depuis GitHub qu'une image dont le SHA-256 correspond et qui porte une signature Ed25519 valide du responsable (la clé publique est dans le firmware, la privée ne quitte jamais le responsable) ; un fichier envoyé à la main par un administrateur dans le panneau reste accepté. Les pages et messages du panneau sont en huit langues.
* **Appairage Bluetooth avec un PIN :** le PIN à six chiffres est dérivé du code de configuration, imprimé sur la console USB et montré à un administrateur dans le panneau ; le téléphone doit le saisir avant de pouvoir écrire les réglages du nœud.
* **Pas encore :** le firmware qui tourne sur une carte, le matériel, toute mesure d'une installation réelle et toute commutation.
* **Seule la maison peut utiliser le panneau :** le panneau répond par défaut au réseau privé (ou aux réseaux que son propriétaire indique, comme un VPN), refuse une liste qui exclurait celui qui l'enregistre, compte les mauvais mots de passe par personne et par adresse, et fonctionne derrière le proxy du serveur pour être atteint depuis l'extérieur sans publier le nœud.**Les estimations le disent :** un canal fait à partir d'une entrée de courant porte `power_estimated` ; Studio et le serveur affichent sa puissance comme une estimation (puissance apparente à partir d'un courant et d'une tension nominale) et signalent une valeur réseau qui en contient une.
* **Liaison chiffrée facultative avec le broker (TLS) :** désactivée par défaut, le nœud parle donc MQTT non chiffré comme avant. Un interrupteur et une zone pour le certificat de l'autorité propre à l'installation (PEM, 2 Ko au plus) dans la page Broker lui font se connecter en `mqtts://`, ne faire confiance qu'à cette autorité et vérifier que le certificat du broker a été établi pour l'adresse atteinte ; ce qui n'est pas un seul certificat PEM est refusé. Couvert par des tests sur ordinateur ; la poignée de main n'a pas encore été exécutée sur une carte.
* **Rapport de santé :** environ une fois par minute le nœud publie depuis combien de temps il tourne et pourquoi il a redémarré la dernière fois, sa mémoire libre, sa liaison et son signal Wi-Fi, combien de fois il s'est connecté au broker et la dernière erreur notée (`armor/health/<nœud>`, le contrat `node_health` d'ARMOR-COMMON). Le code qui le construit est partagé par tous les nœuds et testé sur un ordinateur ; il n'a pas tourné sur une carte.
* **Commande depuis le serveur :** Studio peut redémarrer le nœud proprement, le mettre en pause jusqu'à une heure (il reprend seul), mettre son travail en veille (sans fin, conservée aux redémarrages, le nœud reste connecté et continue de répondre), le faire se signaler, demander un instantané de diagnostic et le marquer en maintenance (`armor/control/<nœud>/...`, le contrat `node_control` d'ARMOR-COMMON). Chaque ordre se fait en deux temps avec un jeton à usage unique propre à cette opération, seul le serveur peut les écrire sur le broker, et il n'existe ni réinitialisation d'usine, ni effacement, ni sommeil profond. Tant que le nœud est en pause ou en veille, tous les relais sont ouverts et aucun ne bouge, quoi qu'on demande (le panneau, le broker ou une centrale) ; les compteurs continuent d'être lus. Le code est partagé par tous les nœuds et testé sur un ordinateur ; il n'a pas tourné sur une carte.
* **Des mesures qui disent leur fraîcheur :** chaque canal de compteur porte l'âge de sa mesure et le nombre de requêtes échouées vers le compteur, si bien que le serveur distingue un vrai 0 W d'un compteur qui ne répond plus ; et une tâche des compteurs bloquée redémarre la carte grâce au chien de garde.
* **Diagnostic sans câble :** après un démarrage qui n'était pas propre (plantage, chien de garde, chute d'alimentation, allocation échouée), le rapport de santé en donne la raison pendant dix minutes ; un plantage laisse son vidage mémoire (core dump) à consulter, télécharger ou effacer depuis le panneau ; les réglages enregistrés sont copiés avant chaque installation de micrologiciel et récupérés si une mise à jour les abîme ; et un administrateur peut demander au nœud, via le serveur, un paquet de diagnostic ou son journal avec un jeton à usage unique.
* **Verrouillage de relais :** un relais peut en désigner un autre qui ne doit jamais être fermé en même temps (les deux sources d'une charge, les deux sens d'un moteur) ; la demande de fermer l'un alors que l'autre est fermé est refusée depuis le panneau, le broker et la centrale, et le nœud n'ouvre jamais l'autre de lui-même. Testé sur un ordinateur ; non exécuté sur une carte.
* **Lecture seule des réglages propres des compteurs :** après la première bonne lecture et tous les `bus.check_settings_s` (600 s, 0 = jamais), le nœud lit les registres holding du PZEM (004T v3 : seuil de puissance et adresse ; 017 : seuils de tension, adresse et shunt) dans le même tour que les mesures, et avertit si l'adresse enregistrée n'est pas celle qu'il utilise ; rien n'est écrit dans le compteur.
* **Scellement facultatif des secrets enregistrés :** désactivé par défaut (`security.seal_secrets`, ou `nodes.nvs_encryption` du serveur). Activé, les mots de passe, jetons, clés, codes PIN et empreintes de mots de passe que le nœud enregistre sont scellés avec ChaCha20-Poly1305 et tout le reste reste lisible ; l'activer ou le désactiver les réécrit sans rien perdre, même si le nœud s'arrête en route. Aucun eFuse n'est brûlé et la flash n'est pas chiffrée : la clé est dans une partition de la même puce, donc cela ne protège **pas** contre quelqu'un qui tient la carte, et un nœud installé avant cette partition garde ses secrets tels quels jusqu'à ce qu'il soit flashé avec la nouvelle table de partitions.

## 🖼️ Captures d'écran

Capturées en 1920×1080 sur un banc d'essai, avec des données d'exemple et sans installation réelle derrière.

<table>
<tr><td width="50%" valign="top"><img src="images/screenshots/overview.png" alt="Résumé : l'état du nœud en un coup d'œil - réseau, broker, firmware et ce qu'il remonte." width="100%"><br><sub>Résumé : l'état du nœud en un coup d'œil - réseau, broker, firmware et ce qu'il remonte.</sub></td><td width="50%" valign="top"><img src="images/screenshots/meters.png" alt="Compteurs : les compteurs d'énergie et les voies analogiques lus par le nœud, chacun avec son nom et son échelle." width="100%"><br><sub>Compteurs : les compteurs d'énergie et les voies analogiques lus par le nœud, chacun avec son nom et son échelle.</sub></td></tr>
<tr><td width="50%" valign="top"><img src="images/screenshots/readings.png" alt="Mesures : ce que le nœud lit en ce moment, avec l'âge de chaque valeur." width="100%"><br><sub>Mesures : ce que le nœud lit en ce moment, avec l'âge de chaque valeur.</sub></td><td width="50%" valign="top"><img src="images/screenshots/zigbee.png" alt="Radio Zigbee : le pont série vers le module coordinateur, quel client peut l'utiliser et l'état du pont." width="100%"><br><sub>Radio Zigbee : le pont série vers le module coordinateur, quel client peut l'utiliser et l'état du pont.</sub></td></tr>
</table>

## 🧩 Rôle dans A.R.M.O.R.

ARMOR-NODE-ELECTRICAL mesure le réseau AC et DC de la maison avec ses compteurs et garde les règles pour commuter entre sources. Il publie les mesures pour [ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER), qui garde leur historique et l'énergie de chaque jour et déclenche les alarmes ; le menu *Électrique* et le *Concepteur électrique* d'[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO) et l'écran électrique d'[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL) le montrent. La commutation est conçue (armer, un jeton à usage unique, l'interverrouillage, la confirmation des deux contacteurs) mais pas activée.

## 📂 Structure du dépôt

```text
ARMOR-NODE-ELECTRICAL/
├── main/    the ESP-IDF component: app_main, electrical_manager (the task), uart_bus, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    pzem (frames of the PZEM meters), pzem_bus (the line), meter_runner (one turn of the loop), electrical_json (the message), electrical_config (the settings),
│            interlock (the rules for switching, NOT linked into the firmware), ble_frame + ble_dispatch (Bluetooth), auth, netplan, board_s3, json...
├── panel/   the web panel: index.html, app.js, text.js (8 languages), style.css
├── tools/   build_node.sh, check_sdkconfig.py, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs, the shared CI checks (armor_ci_validate.py, armor_project_tool.py, _armor_readme_parity.py)
├── secrets/ node.conf.example (the real files are git-ignored)
├── CMakeLists.txt, partitions.csv, sdkconfig.defaults, sdkconfig.board.*   the ESP-IDF project, the flash layout and the settings of each board (s3-eth, s3-wifi)
├── tests/   test_meters, test_runner, test_config, test_mqtt_tls, test_ble, test_interlock, emit_samples + check_samples.py (the messages against ARMOR-COMMON)
├── docs/    DESIGN, NODE_FIRMWARE, BLE_PROVISIONING, SAFETY, SWITCHING, PROTOCOLS, ELECTRICAL_MESSAGES, HARDWARE, BASE_BOARD, ZIGBEE, BENCH_TESTS, SETUP_CODE
└── images/  brand assets
```

## 🛠️ Environnement de développement

**Prérequis:** un compilateur C++17 pour les tests sur l'ordinateur, le conteneur ESP-IDF 5.5.5 pour compiler l'image, un ESP32-S3-WROOM-1 N16R8 ou un Waveshare ESP32-S3-ETH

```bash
cmake -S tests -B build/host && cmake --build build/host && ctest --test-dir build/host   # 380,189 checks: the meters (69), the loop that asks them (81), the settings (93), the readings and inputs (479), Bluetooth (68), the shared base (2,179), the control orders and diagnostics (1,799), the limits (260), the central link (19), the encrypted link (45), the release and signature checks (61), the interlock (135,736) and the switch sets (239,300)
build/host/emit_samples | python tests/check_samples.py                                     # the messages, against ARMOR-COMMON
node tools/panel_mock.mjs --user admin:adminpass123                                         # the panel without a board
tools/build_node.sh generic                                                                 # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth                                                          # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet)
```

See the [firmware guide](docs/NODE_FIRMWARE.md) and the [Bluetooth channel](docs/BLE_PROVISIONING.md).

Voir la [conception](docs/DESIGN.md), les [notes de sécurité](docs/SAFETY.md), les [règles de commutation](docs/SWITCHING.md), les [protocoles](docs/PROTOCOLS.md) et les [messages](docs/ELECTRICAL_MESSAGES.md).

## 🔗 Projets liés

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) est un système de sécurité périmétrique composé de dépôts indépendants. Chacun a sa propre version, ses propres tests et son propre README ; voici la famille :

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Contrats de messages, validateurs, vecteurs de conformité et types générés
* **[ARMOR-NODE-RADAR](https://github.com/JuanenRac/ARMOR-NODE-RADAR)** - Firmware du nœud de terrain pour ESP32-S3 avec trois radars et son propre panneau web
* **[ARMOR-NODE-SOLAR](https://github.com/JuanenRac/ARMOR-NODE-SOLAR)** - Protocoles des onduleurs et batteries solaires et messages d'un nœud passerelle
* **ARMOR-NODE-ELECTRICAL** (ce dépôt) - Nœud électrique : compteurs, le message des mesures du réseau et les règles de commutation
* **[ARMOR-NODE-ALARM](https://github.com/JuanenRac/ARMOR-NODE-ALARM)** - Nœud et centrale d'alarme : zones, armement, temporisations, sirène et PIN, avec ou sans le serveur
* **[ARMOR-NODE-HMI](https://github.com/JuanenRac/ARMOR-NODE-HMI)** - Panneau tactile : l'état du système sur un écran mural, armer et acquitter, et la maison de l'assistant vocal
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - Le réseau local : ses appareils, internet et ce qui change
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordinateur central : télémétrie, alarmes, appareils, relevés solaires et caméras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Console web : caméras, radar, alarmes, énergie solaire et concepteur de site 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Client Android de l'opérateur avec radar 2D/3D en direct
* **[ARMOR-IOS-CONTROL](https://github.com/JuanenRac/ARMOR-IOS-CONTROL)** - Client opérateur iPhone
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Politique d'inférence visuelle qui explique ses décisions et n'agit jamais
* **[ARMOR-VISION](https://github.com/JuanenRac/ARMOR-VISION)** - Zones de caméra, masques et règles de détection pour la Jetson Orin NX
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Intentions vocales hors ligne avec une confirmation impossible à falsifier
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Boîtiers, électronique et matrice d'acceptation sur banc
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Déploiement, banc d'essai CM5, sauvegarde et TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Simulateur de télémétrie hors ligne avec des pannes reproductibles
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Détecte, installe et met à jour les propres dépôts de l'écosystème
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architecture, base de sécurité et matrice des capacités

## 📚 Documentation et communauté

Pour en savoir plus :

* [Matrice des capacités : ce qui est prouvé et ce qui ne l'est pas](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Catalogue des projets : versions et dépendances entre les dépôts](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Historique des modifications de ce dépôt](CHANGELOG.md)
* [Licence (GPL-3.0-or-later)](LICENSE)
* Questions, idées et rapports : electrohobby3d@gmail.com

## 👤 AUTEUR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCE

GPL-3.0-or-later - voir [LICENSE](LICENSE).
