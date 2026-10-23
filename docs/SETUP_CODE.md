# The setup code of a node

Every A.R.M.O.R. node has a **setup code**. It is what lets you take a node that has no administrator yet (a freshly flashed one, or one that was erased) and make it yours. This page explains where the code comes from, which image gives which kind of code, and how to read it. Most confusion comes from one fact: **there are two kinds of image, and they give two kinds of setup code.**

## What the code opens

* **The first administrator.** The node's panel and the phone app ask for the code once, to create the administrator and its password. After that the code is not accepted for that any more.
* **The set-up Wi-Fi.** A node with no administrator opens the network `ARMOR-SETUP-xxxxxx` (the last six digits of its MAC). The password is the setup code.
* **The rescue network.** Afterwards the node keeps its own network (`ARMOR-<KIND>-xxxxxx`) so it can always be reached; its password is still the setup code until you change it in the panel.
* **Bluetooth pairing.** The phone asks for a six-digit PIN. It is derived from the setup code and from nothing else, so the node stores nothing more. The node prints the PIN together with the code.

The code is made of capital letters and digits without the ones that look alike (no `0`, `O`, `1`, `I` or `L`).

## Where the code comes from

The node looks for its code in this order and uses the first that exists.

| # | Source | Length | Stable? | Which image |
|---|---|---|---|---|
| 1 | **Derived from the fleet secret and the MAC** (HMAC-SHA256 of the MAC with the secret) | 10 | Yes: the same on every start, after an erase, and you can work it out again at any time | An image built **with** the fleet secret (named `...-local-...`) |
| 2 | **Fixed in the image** (`CONFIG_ARMOR_SETUP_CODE`, at least 8 characters) | 8 or more | Yes: the same on every board running that image | A custom build that sets it |
| 3 | **Random, kept in the node's flash** | 8 | The same on every restart, but **a new one after the whole flash is erased** | The **public** images: every file of an official release on GitHub, and any image built without the fleet secret |

### Public images (official releases)

The images attached to a release are built **without** any secret, because anybody can download them: an image that held the fleet secret would give the setup code of every board of the fleet to whoever read it. So a node running a public image makes up its own random code the first time it starts, keeps it in its flash and shows it on the USB console. **It cannot be calculated from the MAC.** If you erase the whole flash, the next start makes a new one.

### Local images (your own fleet)

If you build the image yourself with your fleet secret (`ARMOR_WITH_FLEET_SECRET=1`, see `tools/build_node.sh`), the image is named `...-local-...` and **must never be published or attached to a release**; the build and the release tools refuse a public image that carries the secret. A node running it derives its code from its MAC and the secret, so you can always work it out again, also after erasing the flash.

## How to read or work out the code

* **Any image: the USB console.** While the node has no administrator it prints, every 15 seconds, a line that starts with `SETUP:` and says where to go, the setup code and the Bluetooth PIN. Connect the board's USB port and open the serial monitor (for example `idf.py monitor`). Opening the port may restart the board: the line appears again within 15 seconds.
* **Local image only: calculate it.** Use the calculator of the ARMOR-NODE-RADAR repository (`tools/setup_code.html`, opened in a browser: no server, no network; or `tools/adopt_node.py`): type the fleet secret and the MAC and it shows the code, because every node derives it the same way. The panel's set-up screen shows the node's MAC. This does **not** work for a public image, whatever secret you type.

## The Bluetooth PIN

Pairing a phone over Bluetooth asks for a **six-digit PIN**. It is not a second secret kept by the node: it is **derived from the setup code and from nothing else**, with a one-way mix, so the PIN does not give the code back and the node needs to store nothing more. The consequences are worth knowing:

* **The PIN follows the code.** A node whose code is calculated from the fleet secret (local image) has a PIN that stays the same for ever. A node on a public image has a PIN that comes from its random code: it stays the same across restarts and **changes when the flash is erased**, because the code changes.
* **It is the same at every pairing.** The PIN is **not** random for each pairing. A random PIN for each pairing would be better against someone who has seen the PIN once, but the node would have to show it to the person pairing, and most nodes have no screen; it is a known limit, not a promise. What protects the pairing meanwhile: the link is encrypted and authenticated (LE Secure Connections), five failed pairings close the door for five minutes, and by default the pairing window is open only while the node has no user (see `docs/BLE_PROVISIONING.md`).
* **Where to read it.** On the USB console, in the same `SETUP:` line as the code; an administrator can also read it in the node's panel once the node is set up. The PIN opens Bluetooth pairing only; it does **not** open the web panel and it is not the setup code.
* **If somebody has seen it.** Erase the flash (a public image then makes a new code and therefore a new PIN), or change the fleet secret and flash a new local image.

## Which image do I have?

* Downloaded from a release, or built without `ARMOR_WITH_FLEET_SECRET=1` → **public**: random 8-character code, read it on the console.
* Built by you with the fleet secret (file name contains `-local-`, kept in `dist/local/`, never in `dist/` next to the public ones) → **local**: 10-character code you can calculate.
* A board that ran a local image and was then flashed with a public one **changes kind**: its old calculated code stops working and it makes up a new random one.

## Getting the code and the PIN with a public image, step by step

A public image never shows the code anywhere but the USB console, so this is the way in:

1. **Connect the board's USB port to a computer.** On the Waveshare ESP32-S3-ETH use its USB-C port. The board appears as a serial port (`COM5`, `COM6`... in the Windows Device Manager; `/dev/ttyACM0` on Linux; `/dev/cu.usbmodem...` on macOS).
2. **Open a serial monitor at 115200 baud.** Any of these works:
   * `python -m serial.tools.miniterm COM6 115200` (needs `pip install pyserial`; Ctrl+] leaves)
   * `idf.py -p COM6 monitor` (ESP-IDF)
   * `screen /dev/ttyACM0 115200` on Linux or macOS
   * the serial monitor of the Arduino IDE, PuTTY or any other terminal
3. **Wait up to 15 seconds, or restart the board** (press the RESET or EN button, or unplug and plug the USB cable). The node prints this line at start and then every 15 seconds while it has no user:

   ```text
   SETUP: this node has no user yet. Open http://<its address>/ (or join the Wi-Fi "ARMOR-SETUP-56318F" and open http://192.168.4.1/) and enter the setup code U5RF38NM (to pair over Bluetooth the phone asks for the PIN 408376)
   ```

   Here the **setup code** is `U5RF38NM` and the **Bluetooth PIN** is `408376` (yours will be different).
4. **Use them.** Join `ARMOR-SETUP-xxxxxx` with the code as the password (or open the node's address), open `http://192.168.4.1/` and enter the code to create the administrator; or pair the phone over Bluetooth with the PIN. The node does not need to be restarted again for the code to stay the same: it is kept in its flash.
5. **Write it down now.** After the administrator exists the line stops appearing. The code stays valid as the password of the node's rescue network, and an administrator can read the PIN in the panel. If you lose both and the administrator password, erase the flash and flash the image again: the node then makes a new code and a new PIN, and prints them on the first start.

If the console shows nothing, check that you opened the **right** port (unplug the board and see which one disappears), that no other program (for example a flashing tool) holds the port, and that the speed is 115200. Opening the port may restart the board and the first lines go by quickly; the `SETUP:` line comes back every 15 seconds.

## It says the code is wrong

1. **Check the image kind.** A code calculated from the fleet secret never works on a public image (and the other way round).
2. **Read it again from the console.** After an erase of the flash a public image has a new code.
3. **Check the node.** Several nodes may be on the same network: the name `armor-xxxxxx` and the Wi-Fi `ARMOR-SETUP-xxxxxx` carry the last six digits of the MAC, so you can tell which one you are talking to.
4. **Check the characters.** Capital letters and digits only, with no `0`, `O`, `1`, `I` or `L`. The Bluetooth PIN is a different thing (six digits) and does not open the web panel.
5. **It only works while the node has no administrator.** Once one exists, the code is no longer a way in; if the administrator's password is lost, erase the flash and start again.

## Keep it safe

The code opens a node that has no administrator, and the rescue network afterwards. Treat it like a password: do not put it in a repository, a log you share or a screenshot. The fleet secret is worth more: with it anyone can calculate the code of every node of the fleet, so it stays on your machine (the files under `secrets/` are ignored by git).
