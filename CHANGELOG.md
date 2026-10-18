# Changelog

All notable changes to this project are documented here.

## [0.3.8] - Health report

- Every node publishes a health report about once a minute to armor/health/<node> (the node_health contract): uptime, why it last restarted, free and least free heap, the link and Wi-Fi signal, how many times it connected to the broker, tasks short of stack and the last error it logged.
- The report is built by core/node_health.hpp and main/node_health.cpp, shared by the five nodes through ARMOR-COMMON's firmware base; its decisions are covered by the host tests of tests/test_base.cpp. It has not run on a board.
- The broker link counts its connections for the report.

## [0.3.7] - An optional encrypted link to the broker (TLS)

- New settings mqtt.tls (off by default) and mqtt.ca_pem (one certificate in PEM, at most 2 KB; anything else is refused). With TLS on, the node connects with mqtts://, trusts only that authority and checks that the broker's certificate was made for the address it reached; it waits for its clock first (up to two minutes), because certificates have dates. With the switch off nothing changes: the node talks plain MQTT as before.
- The Broker page of the panel has a switch and a box for the certificate of the installation's own authority, in the eight languages, with the new error texts. The saved backup brokers must be mqtts:// too while the switch is on.
- Host tests of the settings: defaults off, the JSON round trip, line ends from a browser, and the refusal of a text that is not PEM, a chain, a text that is too long and a plain address with TLS on. The handshake has not been run on a board.

## [0.3.6] - A channel made from a current input says that its power is an estimate

- The channels made from the node's own current inputs carry power_estimated: true, so a consumer can show their power as an estimate (apparent power from a current and a nominal voltage) and not add it to measured totals without saying so.

## [0.3.5] - Only the house may use the panel, wrong passwords counted per person, and a panel that works behind the server's proxy

- **The panel answers only the house.** A new setting, *Networks that may use this panel* (`web.admin_sources`), says which addresses may sign in or change anything: empty means the private ranges of a house (10/8, 172.16/12, 192.168/16, link-local, loopback and 100.64/10, where Tailscale lives), otherwise the list of addresses and networks you write (`192.168.1.0/24, 10.66.0.0/24`). Any other address is told `outside_network`, except the public look at the node's identity, so a port opened by mistake on the router does not let the Internet try passwords here. A list that would shut out the person who is saving it is refused (`would_lock_out`).
- **Wrong passwords are counted for the person too**, not only for the address: five wrong passwords for a name slow that name down from any address (and, behind the server's proxy, where every request has the server's address, one mistyped name no longer stops everybody).
- **The panel works behind the server's proxy:** its page, its style, its script, its calls and the firmware upload use relative paths, so the same panel opens at `http://node/` and at `https://house/api/v1/nodes/<id>/panel/` (the Bluetooth and map sockets keep their own paths).
- **The fingerprint of the node's certificate is public:** `GET /api/v1/session` carries `cert_sha256`, which is what the server pins before it opens the panel of an HTTPS node; the panel already shows it in the web card so that it can be compared by hand.
- The new setting, its hint and the two new refusals are in the panel in the eight languages. Tests: 376,272 checks in the host test programs (new ones cover the list of networks, the key of a person and the setting).
- **A login or a set-up must carry the marker of the panel** (`X-Requested-With`), like every request that changes something, so a page of another site cannot sign a person in to an account it chose; the server's own sign-in to a node (update, adoption) sends it.
- **The current password of the account is counted too:** wrong ones in "change my password" are slowed down like those of a login, because a stolen session could repeat them.
- **A user change is checked before it is applied:** a role is no longer changed and then the new password refused.

## [0.3.4] - Long polling periods keep their readings, the central's relay route answers and a broker cannot toggle a load

- **A long period between rounds no longer empties the message.** A reading counted as fresh for ten seconds only, so with a period above ten seconds every reading was already too old when the message was made and the meters showed as *waiting*; it now counts for a whole cycle (plus the time of the round and a margin), and a meter that is unplugged is still dropped by the second message after that. The counter of messages counts on both lines.
- **`POST /api/v1/remote/relay` is served** (it had a handler but no route and answered 404): a central with the node's token can move a relay when there is no broker.
- **A TOGGLE from the broker is refused** as the changelog of 0.2.7 said it was (the broker may deliver a message twice and the relay would end up the wrong way round); ON and OFF are unchanged and the node's own API can still toggle. Shared with the other nodes.
- **The converter is the analog task's while it samples:** a login (which asks for random bytes) can no longer disturb a window of readings and put a false peak in the voltage, the current or the energy; the analog task is watched by the watchdog and runs below the tasks of the network and the relays.
- An exception answered by another meter on the line is no longer taken for the answer of the meter asked.
- A button of the board can be turned off at build time (*BOOT button erases the settings*), for a node in a place anyone can reach.
- HTTP answers 408 and 423 with their own status lines, a user change that could not be written is told as a storage error, and the comments say eight languages.
- **The panel tells every error** the node can answer with in the eight languages (the updater's, the reset's, the relay's and the rest), and a test fails when a new code has no text.
- `secrets/node.conf.example` no longer carries a usable set-up code or the address of a real network.
- The documents say what the node does today: two serial lines, how long a reading counts, the Bluetooth pairing with its passkey, the relays moved by the broker and by the token API, that the analog channels are estimates whose energy restarts with the node, the stop bits of the PZEM-017 (to be tried on the bench) and which board is the reference.
- Shared with the other nodes: Improv over USB on a board with a cable now also tells the node to use the Wi-Fi it was given, the upload by hand is refused while an installation or a copy of the slot runs, and the headers of the signature code name the real generator script.
- The Bluetooth note of the panel no longer calls the pairing *just works*: it says what the pairing asks for and what protects it.
- Tests: 376,195 checks in eleven programs plus the panel texts; new ones cover long periods (11, 30 and 120 seconds), the refusal of a TOGGLE from the broker and the exceptions of other meters.
- Shared with the other nodes (from ARMOR-COMMON's firmware base): the pairing of Bluetooth closes after repeated failures, the random generator keeps away from the converter, a user write is all-or-nothing, a repeated key in a JSON document is refused and four-number firmware versions are understood.

## [0.3.3] - The Bluetooth PIN in the panel

- **Signed firmware:** a node installs from GitHub only a firmware whose signature verifies. Each release carries, next to the image and its SHA-256, an Ed25519 signature of that hash (`<image>.sig`); the node downloads the image, hashes it and installs it only when the hash is the published one and the signature verifies with the public key of the releases, which is in the firmware (the private key never leaves the maintainer's computer). A release without a signature, or with a wrong one, is refused and nothing changes.
- An administrator can still upload a firmware file by hand in the panel, as before: that is the administrator's own act and is the way out if a key ever has to change.
- The panel tells the two new refusals (no signature, bad signature) in the eight languages.
- The verification (SHA-512 and Ed25519, with their constants worked out by a script instead of typed) is tested on a computer against signatures made by another implementation, with every kind of damage to the message, the signature and the key.
- An administrator can read the Bluetooth PIN in the panel (Bluetooth card, *Show the Bluetooth PIN*), without going to the USB console: it is asked of the node only then and forgotten when hidden; new route GET /api/v1/ble-pin, administrators only; the texts are in the eight languages.

## [0.3.2] - A PIN for Bluetooth pairing

- The Zigbee guide lists the radio modules that work with the bridge and names the RF-star RF-BM-2652P4 (TI CC2652P7: twice the memory, Zigbee 3.0, Thread, Matter and Bluetooth 5.3) as the module for the first boards, with its supply, reset, JTAG, antenna and footprint notes; the older CC2652P modules stay supported as before.
- Bluetooth pairing now asks for a 6-digit PIN that the node prints on its USB console next to the setup code; the link is authenticated and nothing is accepted over Bluetooth without it (until now any phone in range could pair without a PIN).
- The Bluetooth note of the panel explains the PIN in all eight languages.

## [0.3.1] - Portuguese in the panel and a warning for the access point and the station at once

- The panel speaks Portuguese, the eighth language, in every page, hint and message of the settings.
- The overview warns when the node's own Wi-Fi network and its connection to your router are on at the same time (they share one radio and one channel), and the Wi-Fi help explains what that costs and when to switch the access point off.

## [0.3.0] - Slot copy, background update check and confirmed image rules

- A firmware slot can be copied to the other one from the panel, and the update of a node waits for the image to be confirmed only after the node has an address or its own access point is reachable, rolling back after five minutes otherwise.
- The station connection that was paused to leave the access point free retries after ten minutes at most instead of waiting forever.
- The firmware check against GitHub runs in the background and the panel asks for its result, so a slow search no longer freezes the panel.
- Saving the settings is serialised, so two browsers saving at once cannot mix their changes.
- Logging out needs the page's own token, which stops another page from logging the panel out.

## [0.2.9] - Copy-log icon and copy of a firmware slot to the other

- **Copy one firmware slot to the other:** next to the switch between ota_0 and ota_1, the Firmware card of the panel has a button that copies the firmware that runs over the other slot (task of its own, with a percentage, the image checked at the end, no restart, no change of the boot slot); `POST /api/v1/ota/copy` starts it and `GET /api/v1/ota/copy` tells how far it is. The slot sizes shown by the status are measured again after a copy or an update.
- **Node panel:** the log of the panel has a small copy icon (two sheets) that puts the whole log on the clipboard and says "Copied" for a moment; the same panel code is shared by all the nodes.

## [0.2.8] - The search on GitHub finds memory for its connection

- Looking for a new version on GitHub no longer fails with connect_failed when the node was short of internal memory (a browser on the HTTPS panel at the same moment): the TLS buffers now live in the external memory, and the search and the download try three times.

## [0.2.7] - Hardening of the web server, the broker link and the sessions

- The web server gives up a request that announces a body and sends nothing (or an upload that stalls) after fifteen seconds, instead of keeping its only thread for ever.
- The restart after an installation from GitHub no longer depends on a browser asking how far it is: a timer of the node watches the installation and restarts it when it is done.
- A "remember me" session is no longer taken for free after thirty minutes when the table fills up, and a session token that starts again replaces the old one instead of living twice.
- On a saved broker that is not the first one, the node tries the first one again every half hour instead of forgetting it until the next restart.
- A TOGGLE from the broker is refused (the broker may deliver a message twice and the relay would end up the wrong way round); ON and OFF are unchanged, and the central with the node's token can still toggle.

## [0.2.6] - No deadlock between the relays and the broker

- **A message is never published while a lock of the node is held:** `mqtt_link::publish()` puts the message in a short queue (48, the oldest dropped first) and a task of its own hands it to the client. Before, a task that held a lock of its module and published could wait for the client's own lock while the client, running the handler of an incoming command, waited for that module lock: both for ever, with nothing to restart the board. The rule "without the broker for N seconds, every relay opens" depends on this.
- **The task watchdog restarts the board:** it panics after 10 s (it only printed before) and the tasks that guard the outputs now report to it.

## [0.2.5] - RS485 with a direction pin, a second serial line and a central that needs no broker

- **A driver-enable pin for an RS485 adapter** (`bus.de`): the PZEM-017 has no TTL side, and an adapter that needs its direction pin driven now has one. The line is put in RS485 half-duplex mode on that pin: raised while the node sends, no echo read back (a request that comes back anyway is still dropped, as before).
- **A second, independent serial line** (`bus2`, UART0: the console is on the USB) with its own RX, TX, driver-enable and speed, and a `line` (1 or 2) on every meter, so that PZEM-004T meters (TTL) and PZEM-017 meters (RS485) work together. An address only has to be its own on its line; the round time is the first line's. A line that cannot be opened leaves the other one and the analog inputs working. The panel's *Meters* page has the second line's card, the driver-enable pin and each meter's line.
- **A direct, authenticated HTTP API for a central** that has no broker to rely on (off by default; `remote.http` and a token the panel can generate and never shows again): `GET /api/v1/remote/state` reads the node (its relays and the last message of the node) and `POST /api/v1/remote/relay` {"relay":"name","action":"on|off|toggle"} moves one relay. The token travels as `Authorization: Bearer <token>`, is compared without leaking where it differs, must be at least 20 characters, and a wrong, missing or unset token answers 401 without saying which. Tested on a computer (`core/central_link.hpp`); the routes have never answered on a board.
- **Waits that grow between tries.** A Wi-Fi network that does not answer is tried again after 1 s, then 2, 4, 8... up to 30 s (a little scatter on every wait, so that many nodes that lost the same router do not all knock together), and the broker after 1 s growing to 60 s; a try that works starts the schedule over. Before, every retry was a fixed 3 s (Wi-Fi) or the client's fixed 10 s (broker). The schedule is `core/backoff.hpp`, tested on a computer.
- **Improv Wi-Fi over the USB cable.** A web page that speaks the Improv standard (ESP Web Tools) can give the node its Wi-Fi network through the same USB socket the log comes out of: the node answers its state, identifies itself, describes the device and takes the SSID and password (checked and stored through the same settings document the panel and the Bluetooth set-up use), then restarts to join. Whatever else is on the line is ignored and a packet is only acted upon whole and with a right checksum. The packets are `core/improv.hpp` (tested on a computer); the USB side has never run on a board.
- **The stack each task has left**, in the status (`tasks`: the least room ever left, smallest first, with a `low` flag under 512 bytes) and as a warning in the log, once per task, before a near overflow becomes a crash. `core/task_health.hpp` (tested on a computer) and `main/task_health.cpp` (FreeRTOS' trace facility, switched on in the defaults).
- **Tests:** the second line in the settings and at work (two stand-in lines with the same address on both), the token and the relay request, the schedule of retries, the Improv packets and the stack report; the panel in a real browser (51 checks).

## [0.2.4] - Every node can update itself from GitHub

- **The node can look for a newer firmware on GitHub and install it** (*Firmware and log -> Check GitHub for a new version*), next to - never instead of - the upload of a file: it asks the repository's latest release, offers it only when it is newer and the release carries the SHA-256 of the image, downloads **the image built for this board** (`armor_electrical-<board>.bin`) straight into the other firmware slot with a progress bar, and installs it only when the hash matches; then it restarts into it. Administrators only; it needs the node to reach the Internet. The code is the one the radar node already had, now shared by every node (`main/github_update.*`, `core/semver.hpp`, `core/release_assets.hpp` in ARMOR-COMMON's firmware base).
- **The panel and its tests:** the *Firmware and log* page has the card in the seven languages, the stand-in node (`tools/panel_mock.mjs`) answers the three routes (`GET /api/v1/ota/check`, `POST /api/v1/ota/install`, `GET /api/v1/ota/progress`), and the host tests check the comparison of versions and, for a release with and without an image per board, that a board never picks an image that is not its own.
- **The base board's pin constants moved to their own file** (`core/base_board.hpp`) so that `core/board_s3.hpp` stays identical in every node.

## [0.2.3] - The relays as devices of the house: the server can switch them

- **A relay is a device of the device layer.** With the new setting `remote.relays` (off by default) every enabled relay publishes `armor/device/<node>/<relay>/state` (`{"on":true}`, when it changes and every half minute) with an `availability` message, and obeys `armor/device/<node>/<relay>/set` with `ON`, `OFF` or `TOGGLE` - the convention the radar node already uses for the pins its panel maps. In Studio the relay is added in *Devices* with the connection **ARMOR node** and the name `<node>/<relay>`; the server then switches it with its own rules (a risk that asks first, only an administrator for the critical ones). Anything else in a command (a number, a sentence, a pulse) is ignored and logged.
- **Three independent things must agree before a relay moves by a command:** the node's setting `remote.relays`, the broker's access list (`mqtt_identity.sh electrical-relays <node> on` in ARMOR-DEVOPS, off by default) and a device the operator created in the server. The panel keeps working as before.
- **Fall back to open.** `remote.link_timeout_s` (0, never, or at least 10): with the broker out of reach for that long, every closed relay opens by itself, once per loss.
- **A node can subscribe to the broker.** `mqtt_link::subscribe(filter, handler, on_connected)` subscribes again on every reconnection and hands each complete message that matches (the MQTT rules for `+` and `#`, `core/mqtt_topic.hpp`) to its handler; a message in pieces is dropped.
- **The panel** has a card on *Inputs and outputs* with the setting, the fall back time and how to add the devices in Studio (seven languages); the settings are checked (it needs a broker; the time is 0 or at least 10 seconds).
- **Tests:** the command words, the topics and the filter, the rules of `+` and `#`, finding a relay by name, the link watch and the fall back, and the settings and their stored form (477 checks on the Wi-Fi profile, 431 on the Ethernet one); the panel's browser test passes 43 of 43.

## [0.2.2] - A second client for the Zigbee radio: an alarm panel that takes over while the server is away

- **A fallback client for the Zigbee radio.** Besides the server that runs Zigbee2MQTT (the primary, `zigbee.allowed_client`), the bridge can name a second address (`zigbee.fallback_client`, for example an alarm panel). The fallback is accepted only while the primary is not connected and has been away for `zigbee.fallback_after_s` seconds (30 by default, 5 to 600); the primary may always connect and takes the radio back from the fallback. Any other address is still refused. The node still interprets nothing: what the fallback says to the radio is its own business. The rules (`Arbiter` in `core/zigbee_bridge.hpp`) are tested on a computer: the wait, the primary's priority, the fallback reconnecting, and the clock of the absence restarting only when the primary leaves.
- **The panel** has the two new settings on the *Zigbee radio* page and shows which client holds the radio (the primary, or the fallback while the server is away), in the seven languages; the status carries `role` and `takeovers`.

## [0.2.1] - The base board: a Zigbee radio, relay outputs and analog inputs

- **A Zigbee radio, lent to Zigbee2MQTT.** The base board carries a Zigbee coordinator module on its own serial line (UART2). The node hands that line over TCP, byte for byte, to **one client** - Zigbee2MQTT on the server - and only to the address named in the settings (`zigbee.allowed_client`, required while the bridge is on); a connection from any other address is closed at once and counted, and a second one from the allowed address replaces the first. The radio's reset and boot-loader lines are driven open-drain (low, or let go) from the panel's two buttons, to restart the radio or to give it a firmware through the bridge. The rules (who may connect, the order and the pauses of the reset) are in `core/zigbee_bridge.hpp`, tested on a computer; `docs/ZIGBEE.md` explains the Zigbee2MQTT lines and how the radio gets its firmware, and lists what is not yet verified on a module.
- **Up to eight relay outputs.** Each has a name, a label, a pin and whether it closes with the pin low. An administrator closes or opens it from the panel (`POST /api/v1/relays/<n>`); **every relay starts open** when the node starts, and the level of a pin is set before the pin becomes an output, so a relay never sees a pulse of the wrong level. They are plain relays closed from the panel: the source-transfer rules of `core/interlock.hpp` are still not linked into the firmware.
- **Up to eight analog inputs**, read by ADC1 with the chip's calibration: a voltage (through a divider) or a current (from a clamp or a Hall sensor), as an average (DC) or an effective value (AC), with a scale, a zero offset, a noise floor and, for a current, an optional nominal voltage that gives a power and an energy. Each becomes a channel of the node's message after the meters', with only what it measures, and what leaves is kept inside the contract's ranges (a voltage is never negative). A node with only analog inputs and no meter now publishes too.
- **Two new panel pages**, in the seven languages: **Zigbee radio** (the settings, the bridge's state and counters, the reset and boot-loader buttons, and the lines for Zigbee2MQTT's `configuration.yaml`) and **Inputs and outputs** (the relays with their live state and buttons, and the analog inputs with what the pin reads). Their help topics, tooltips and error texts are in the seven languages too.
- **The settings are checked before they are stored:** the radio's pins, a relay's pin and an input's pin may not be reserved by the board, shared with the meters' line or with each other; an analog input must be on an ADC1 pin (GPIO 1 to 10); names are unique among the meters and the inputs; the allowed address must be a usable unicast address; the meters and the inputs together may not pass the sixteen channels one message holds. A document from before the base board loads as it was, with all of it off.
- **The board profile** carries the defaults: on the Ethernet module the radio's control lines are on GPIO 43 and 44 (the chip's ROM prints on those pins at every reset, so no relay goes there), relays on 38 to 42, 47, 48 and 21, and analog inputs on 1, 2, 4, 5, 6 and 7. `docs/BASE_BOARD.md` is the whole proposal for the board that carries the ESP32-S3 module, with the module's pins, the reasons and a checklist for the first power-up.
- **Documents:** `docs/ZIGBEE.md` and `docs/BASE_BOARD.md` are new; the firmware, hardware, design and safety pages and the README (in the seven languages) say that the firmware no longer only reads. Nothing here has run on a board.
- **Tests:** a new host test file covers the settings, the relay bank, the analog conversions and channels, the bridge's rules and the stored document (396 checks on the Wi-Fi profile, 350 on the Ethernet one); the node's loop is tested with and without meters and analog inputs; the messages with analog channels are accepted by ARMOR-COMMON's contract. The panel's stand-in node (`tools/panel_mock.mjs`) answers the new routes, its settings were brought up to date (clock, backup networks), and the browser test passes 41 of 41 checks on both boards.

## [0.2.0] - No silence without a clock, clean RS485 readings and a login over HTTP after one over HTTPS

- **An RS485 adapter that hears its own request no longer spoils the readings.** The echo of the request that comes back before the answer, and stray bytes in front of it, are dropped before the answer is read (they used to make every reading "wrong length").
- **A node without a clock no longer falls silent.** It used to drop everything it read until a time server answered, so a node on a network without internet, or one that restarted after a power cut with the provider down, said nothing at all. It now publishes with the time since it started, and ARMOR-SERVER stamps such a message with the moment it receives it; as soon as the clock is set it sends real dates again.
- **The broker link no longer races with itself** when the node moves to another saved broker: the client is replaced under a lock and the old one is stopped outside it, so a message being published at that moment can no longer use a client that is being destroyed.
- **The panel's help** (*Firmware and log*) describes the firmware slots and their switch, the copy of the log, in the seven languages.
- **The node says what kind it is.** The public session answer (`/api/v1/session`) now carries `kind` (`radar`, `solar`, `electrical` or `hmi`) next to the node's id, version and board, so Studio's search for nodes in the network can tell a radar node from a solar one before it is added, even when its panel title cannot be read.
- **Copy the log** (*Firmware and log -> Log*): a button copies everything the log shows to the clipboard, in the seven languages. It also works when the panel is opened over plain HTTP, where the browser's own clipboard is not available, through a fallback.
- **A login over HTTP works after one over HTTPS.** The session cookie of the page served over HTTPS now has its own name (`armor_session_tls`); with one shared name, the browser kept the "Secure" cookie of the HTTPS login and refused to let a plain-HTTP page replace it, so the login over HTTP looked as if it did nothing (it worked in a private window).

## [0.1.9] - Switch between the two firmware slots from the panel

- **Firmware slots in the panel** (*Firmware and log -> Update*): a new card shows the two application slots (ota_0 and ota_1) with the version each one holds and which one runs, and a button boots the other one at the next restart - the way back to the version that ran before an update, or forward to the one just installed. It asks for confirmation, needs an administrator, refuses an empty slot or a firmware of another project, and the settings are kept. The same card exists in the radar, solar, electrical and touch-panel nodes, in the seven languages (`POST /api/v1/ota/switch`).

## [0.1.8] - The log says when the readings cannot be published

- When the meters produce a message and the broker link has not been given to them, the log now says so once (*the readings are not being published*) instead of dropping them without a word.


## [0.1.7] - About and Help pages, date and time, hints everywhere and a node that can always be reached

- **"Keep me signed in on this browser"** at login: unchecked, nothing changes (30 minutes idle still signs out); checked, the session survives closing the browser and lasts 30 days of actual use.
- **About:** the node's own identity, firmware version, author and licence. **Help:** a tabbed page explaining each part of the menu in plain language, in all seven languages.
- **Real bug (found on ARMOR-RADAR, fixed here too):** saving only ever added the document's backup Wi-Fi networks or brokers to the ones already stored, never replacing the list - so removing one and saving brought it straight back. Saving now replaces the list exactly as sent.
- **Date and time:** a new *Date and time* card on the Overview page shows the node's local time and where it comes from (time server, set by hand, or not set yet). The Network page has a new clock card: the time zone (a list of common zones, with summer time handled by itself), the time server on or off, and, with the server off, a button that sets the node's clock from the browser. The clock now starts by itself as soon as the node has an address - before, it only started together with the broker connection, so a node with no broker never had a time. The time server moved out of the broker settings into a `time` section of the settings file; older files that still carry `mqtt.ntp` load as before.
- **Flash memory:** a new card on the Overview page with the flash size, how much the partitions reserve, what is left unassigned and, for each of the two firmware slots, how much of it the image uses, how much is free, which version it holds and which one is running or boots next.
- **A hint on everything:** pausing the pointer over a field, a button or a menu entry now shows a short explanation in the panel's language (all seven).
- **"Load a configuration file"** now lights up under the pointer like the other buttons.
- **Real bug, found on a bench:** a backup Wi-Fi network or broker lost its password the next time the settings were saved, because the form never holds a stored password and an entry that arrived without one replaced the stored one with an empty password. An entry sent without a password now keeps the one it had (same network name; same user on the same slot or address); a different name starts empty. A secured network tried with an empty password answers "no network with compatible security"; that reason is now shown as a wrong password instead of an anonymous failure.
- **Real bug, found on a bench:** the choice of language made in the panel was never written to the node's settings, so the exported file said English while the panel was in Spanish. Choosing a language now stores it at once, with no restart.
- **Real bug, found on a bench:** the node's rescue network disappeared right after signing in to the panel through it. While the node kept looking for the Wi-Fi network it could not find, every attempt scanned all channels, and the access point shares the radio, so the phone lost the network. While someone is connected to the rescue network the node now pauses those attempts and resumes them ten seconds after the last person leaves.
- **Real bug, found on a bench:** after the first set-up by Wi-Fi the node's own network was never seen again. The set-up code was erased the moment the administrator was created, and that code is the password of the node's own and rescue networks, which could then not start. The code is now always kept (the one derived from the fleet secret, the one built into the image, or a random one stored in flash so it is the same at every start), and the set-up screen accepts a Wi-Fi network for the node to join on every board, also on those with a cable.
- Code only in this release - there is no board of this kind to flash it on yet.

## [0.1.6] - Chip, flash, PSRAM and bootloader version on the Overview page

- **A new "Hardware" card:** the chip and its revision, how many cores, the flash and PSRAM sizes, and the IDF version of both the running firmware and the bootloader (not the same thing - a mismatch there usually means the wrong board file was built). Read straight from the chip and the bootloader's own descriptor, not stored anywhere. Needed `spi_flash` added to the component's own dependencies; built clean with the real toolchain.

## [0.1.5] - An eye on the login and set-up passwords

- **Sign-in, the admin password and the Wi-Fi password at set-up** now have an eye button that shows what was typed - the one place a mistyped password locks someone out with no other field to check it against.

## [0.1.4] - Download and load a whole configuration

- **An admin can now download this node's whole configuration** (Network page, secrets included) as a .json file - the same document the flash keeps - and load one back into the form before saving. Built for setting up a batch of identical boards from a single bench node instead of retyping Wi-Fi, broker and the rest by hand. The node's own identity (its node ID) is never overwritten by an import: every board keeps its own. `GET /api/v1/config/export` is a new, admin-only route; loading a file changes nothing until the existing Save button is pressed.

## [0.1.3] - A periodic restart the panel can turn on

- **A new System setting:** "do not apply" or every 1, 2, 3, 4, 6, 12, 24 or 48 hours. It is the same clean restart as every other one (Wi-Fi told it is leaving before the radio powers down), just timed instead of triggered by a save or a firmware update - for a board nobody is going to touch for weeks. Settings-schema change only (`system.auto_restart_hours`, 0 by default): a node left at "do not apply" behaves exactly as before.

## [0.1.2] - Backup Wi-Fi networks and backup brokers

- **Up to 3 backup Wi-Fi networks and 2 backup brokers**, in the panel's Wi-Fi and Broker pages: tried in order, the same way a phone or laptop remembers more than one network, only after the one above has failed to connect for a while - never while it still works. Settings-schema change only (`sta.backup[]`, `mqtt.backup[]`, both empty by default): a node with none configured behaves exactly as before.

## [0.1.1] - A proper goodbye to the access point before restarting

- **The node never told the access point it was leaving before a restart:** `esp_restart()` just cuts the radio, with no deauthentication frame sent; some access points get stuck holding the old association and need restarting themselves before the node can rejoin. It now calls `esp_wifi_disconnect()` and gives it a moment before restarting, on every restart path (the panel, a firmware update, the factory reset held on BOOT).
- **The "Restarting..." screen never appeared after a firmware update:** `S.rebooting = true` was set without calling `render()` on that one path - the panel just sat on the old screen until the auto-reload kicked in on its own six seconds later. Now it shows immediately.
- **The default HTTP header limit (512 bytes) was too small for a real browser:** a session cookie plus a modern browser's own request headers can exceed it, which the panel refused outright - a blank page saying "Header fields are too long". Raised to 2048 bytes.

## [0.1.0] - The node finder can tell this is an electrical node

- **The panel's own title now says what it is** ("A.R.M.O.R. electrical" instead of a generic "A.R.M.O.R. node"). ARMOR-STUDIO's "find nodes on the network" reads this when it probes a candidate, so the Electrical menu no longer lists a radar or a solar node found on the network.
- Built with ESP-IDF 5.5.5.

## [0.0.9] - A login that actually leaves you signed in

- **The session cookie was garbage:** it was built in a function's own local variable, and the HTTP server only keeps a pointer to a header's text, not a copy of it; by the time the response was really sent, that memory had already been reused for something else. The login or the first-time set-up answered "ok", but no browser ever kept a real session - re-entering the panel always looked like a fresh sign-in. The cookie is now kept alive until the response goes out.
- Built with ESP-IDF 5.5.5.

## [0.0.8] - Saving the settings over Bluetooth no longer restarts the node

- **Bluetooth task stack:** the task that serves the Bluetooth channel (`ble-worker`) ran out of stack while saving a Wi-Fi network with a fixed address, and the node restarted in the middle of the set-up (the app said the connection was lost). Its stack is now 16 KB instead of 8 KB.
- **Bluetooth is on at every start while there is no address:** a node set up with a cable that is not plugged in, or whose Wi-Fi was not joined, did not advertise at all and could not be found by the app; it now advertises at every start and stops a couple of minutes after it has an address.
- Built with ESP-IDF 5.5.5.

## [0.0.7] - A node that has just been set up can always be reached

- **A rescue access point.** A node that is set up (it has a user) but has no address 90 seconds after it starts - no cable, no Wi-Fi network it can join, no access point of its own - opens the set-up Wi-Fi `ARMOR-SETUP-xxxxxx` again, protected with the set-up code. Before, it closed its set-up Wi-Fi when the first administrator was created and vanished.
- **Why it did not join the Wi-Fi.** The `hello` of the Bluetooth channel and the panel's status carry `sta_error` (`network_not_found`, `wrong_password` or `failed`).
- **Bluetooth stays on while there is no address.** A node set up to join a Wi-Fi network keeps advertising until it has an address, and stops two minutes after it has one, so the app can come back and read the outcome.
- **A fixed address on the Wi-Fi too.** DHCP or a fixed address is valid whichever connection the node uses (it was only honoured on the cable); the panel offers it for both.
- Same change as ARMOR-RADAR 0.3.2; built with ESP-IDF 5.5.5.

## [0.0.6] - A board that boots

- **Boot loop fixed (shared firmware base):** generating the TLS certificate overflowed the main task's stack on the first boot of a board; the buffer is on the heap and the stack is 16 KB. Both images (Wi-Fi and Ethernet) build.

## [0.0.5]

- A GitHub Actions CI baseline (`.github/workflows/ci.yml`): validates the manifest, the version, CHANGELOG.md's heading, the seven README translations' structure and its own local Markdown links, then runs this project's real build/test through `tools/armor_project_tool.py build-test .` (vendored from ARMOR-COMMON, alongside `tools/armor_ci_validate.py` and `tools/_armor_readme_parity.py`, which do the manifest/docs checking).

## [0.0.4] - The commands to a switch, tested and not linked

- **`core/switch_set.hpp`:** the layer between a message and the transfer controller. It reads a command of ARMOR-COMMON's new `electrical_command` schema strictly (`arm`, `close_a`, `close_b`, `open`, `acknowledge`), refuses what it should (`disabled`, `fault`, `not_armed`, `not_confirmed_open`, `unknown_switch`, `bad_token`, `not_supported`) and answers with an `electrical_result`. Closing is two steps: an accepted `arm` gives a one-time 64-bit token and a close must carry it (a wrong token withdraws the arm; the token is spent by the first close); `open` needs neither and always works. The state message can carry `switches` (`SwitchSet::switches_json`, `message_json` takes the array).
- `TransferController::disarm()` (withdraws an unused arm; never moves anything).
- `tests/test_switch_set.cpp` (about 240,000 checks): the reader against the shared vectors, the two-step close and its replays, every refusal, a welded contactor, the state message, and 40 rounds of random commands and noise against contactors that weld and stick; a mutation that skips the token check is caught. `tests/emit_samples.cpp` prints states, commands and answers and `check_samples.py` has ARMOR-COMMON validate them (and that an answer answers a command that was sent).
- **Not linked into the firmware:** no image creates a `SwitchSet`, the state message still says `switching_enabled: false` and has no `switches`. Nothing here has driven a contactor.

## [0.0.3] - Configuration from a phone over Bluetooth, and the meters loop tested

- **Bluetooth Low Energy (NimBLE), the same channel as the other nodes:** a phone running the ARMOR app finds the node as `ARMOR-XXXXXX` and sets up its name, Wi-Fi station, address, broker and the rest, with the same set-up code and the same users as the panel (`docs/BLE_PROVISIONING.md`). New setting `ble.mode` (`setup` by default, `always`, `off`) in the panel's Network page, in seven languages. 68 host checks of the framing, the requests, the access rules and the setting.
- **What the node does on every turn of its loop is now tested on a computer** (`core/meter_runner.hpp`, 62 checks): the firmware's task only opens the UART and gives it the clock. A stand-in line plays the meters: one that reads, a DC one, one that does not answer, one with a bad CRC, another address or a refusal, replies in pieces, replies that come late, a line that will not send, sixteen meters, the message's clock and the polling period.
- **A fix the tests found:** a meter that worked and then stopped answering was shown as *waiting* for ever; it is now *silent* (or *garbled*), with the reason, and it leaves the message after ten seconds as before.
- **The panel's rows** no longer stretch to the tallest field of a row (the same one-line change in the three node projects, kept once in ARMOR-COMMON's `firmware_base`).
- **The panel** was exercised in a real browser (headless Edge) against `tools/panel_mock.mjs`, a stand-in node, on both board profiles: set-up, login, the Meters and Readings pages, saving with a refused and a corrected meter, the Bluetooth setting, every page in the seven languages, the width of a phone. The texts of the solar node that this panel no longer uses were removed.
- **Not done:** the radio side has never run on a board and no phone has talked to it; nothing has run on a board.

## [0.0.2] - The firmware of the electrical node

- **The firmware,** forked from the solar node's and reduced to what a node of meters needs: the settings stored in flash (`core/electrical_config.hpp`), **one serial line** (a hardware UART, 9600 baud by default) for up to **sixteen meters** (PZEM-004T v3 AC or PZEM-017 DC, each with its channel, name, kind and Modbus address), the rounds that ask them one at a time, and the message published on `armor/electrical/<node>/state` (QoS 0, not retained). It **only reads**: no request that writes to a meter exists, no output pin is driven, and the rules for switching are not linked into it.
- **The panel** is the other nodes' (set-up with the code on the USB console, users, Wi-Fi station and access point, broker, update over the air with rollback, log, HTTPS) with two pages of its own, **Meters** (the line and each meter, with its state, its counters and a hint for the usual causes of silence) and **Readings** (the last message, channel by channel), in seven languages.
- **Two boards, one code base:** the ESP32-S3-WROOM-1 N16R8 on Wi-Fi (`s3-wifi`) and the Waveshare ESP32-S3-ETH on its cable (`s3-eth`); the universal images build in the ESP-IDF 5.4.2 container without warnings.
- **Tests:** 71 checks of the settings (defaults, the meters' channels and addresses, the pins of the line, the stored document and what the panel may send) on top of the 53 and 135,736 of 0.0.1.
- **Not done:** running on a board, any meter or contactor connected, any measurement of a real installation, any switching.

## [0.0.1] - The core of the electrical node

- **Reading the meters.** The frames of the Peacefair **PZEM-004T v3** (AC) and **PZEM-017** (DC) over Modbus RTU: the CRC-16, the request that reads the input registers and the decoding of the reply (voltage, current, power, energy, frequency, power factor, alarm). Only the read request is built; the requests that write to a meter (its address, its thresholds, the reset of its energy counter) are not built anywhere. A reply is refused when its CRC, address, function or length do not fit, when it is an exception, or when it holds a value no real meter could give.
- **The bus.** A line with several meters is asked one at a time, in order, with a reply timeout; the last good reading of each is kept, a meter that goes silent is counted and its old reading is not published after ten seconds.
- **The message.** `armor/electrical/{node}/state` (kind `electrical`, one entry per channel: AC or DC, voltage, current, power, energy, and for AC the frequency and the power factor; the state of a switch as the node sees it; an alarm), which the shared contract of ARMOR-COMMON accepts. It carries a state and never a command.
- **The rules for switching**, kept apart from any hardware: a controller for the transfer between two sources that never commands both, needs the switching allowed, the node armed just before, both contactors confirmed open for the whole dead time and a fault-free state, treats a contactor that does not show what it was told as a fault that stays until it is acknowledged with both confirmed open, and always opens at once when asked. **Switching is off by default and nothing drives any hardware.**
- **Tests:** 53 checks of the meters and 135,736 of the switching rules (scripted scenarios and random steps with contactors that weld, stick and fall out), and the messages the node makes are checked against ARMOR-COMMON.
- **Not done:** the firmware of the node, every piece of hardware, any measurement of a real installation, any switching.
