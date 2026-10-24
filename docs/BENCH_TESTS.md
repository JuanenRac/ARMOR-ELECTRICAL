# Bench tests

The software of these checks is done and tested on a computer; what is left needs a real board, a real phone or the CM5. Nothing here has been run on hardware yet.

## Electrical node

| Check | Connect | Run | Expected |
|---|---|---|---|
| Analogue inputs and the panel | one analogue channel | open the panel and log in in a loop for 10 minutes | no restart by the task watchdog, stable readings |
| Freshness of the readings | four analogue inputs | read `reading_age_ms` in the state or in Studio | smaller than `bus.poll_s` |
| PZEM-017 stop bits | a PZEM-017 | try one and two stop bits | a valid answer with the one in the data sheet (two) |
| Continuous ADC | a voltmeter and a known source | enable the continuous conversion with DMA | the calibrated reading matches the voltmeter per input |
| Meter task and the watchdog | meters on the bus | run for one hour | the meter task feeds the watchdog and there is no restart |

## Checks every node needs on a real board

Connect the board by USB, open the console at 115200 baud and note the result of each line (OK or FAIL, with the log).

| Check | Connect | Run | Expected |
|---|---|---|---|
| Bluetooth session isolation | the node and a phone with the app | connect and disconnect over Bluetooth 50 times in a row (open "Set up a node", close it) | no restart (the uptime in the node state never drops); the next session never receives an answer meant for the previous one |
| Settings survive an update | a node with the previous firmware | save settings, then update over the air to a firmware that adds a field or makes a value invalid | it starts, keeps the valid settings, drops only the bad field and says so in the log; it does not fall back to factory settings |
| Update for another board | a node of one board (for example Ethernet) | upload through the web panel the image of the other board of the same project | today the manual upload only compares the project name and accepts it; the update from GitHub must refuse it. After the change both must refuse with 422 |
| Backup broker | a node with two brokers configured | stop the main broker while the backup one is active, then bring it back | no lost readings, no 30 s without data every 30 minutes, and the node returns to the main broker |
| Router that does not answer ping | a node behind such a router | leave it running 10 minutes | the network watchdog does not disconnect the Wi-Fi |
| Network scan on Ethernet + access point | an Ethernet board in the Ethernet + access point mode | start a scan from the panel | the log shows the empty station is not reconnected and the Ethernet link does not drop |
| Log in a small task | any board | write logs in a loop from a task with a 3 KiB stack (panel, Log) | no stack overflow (high-water mark above 512 bytes) |
| Pins during a scan | a pulse generator on a spare pin | pulse the pin while the network scan runs | no blocking and no watchdog restart |
| Set-up from the browser | a new board and ESP Web Tools | install and give the Wi-Fi credentials | the page says "provisioned" only after the node joined the network and shows the panel address; with a wrong password it says it could not connect after 45 s |
| Session MAC | a wired board | open `/api/v1/session` | the MAC is the one printed with the set-up code on the console |
| Sealed secrets | a node with the option | turn `security.seal_secrets` on and restart | the secrets keep working; without eFuses the key stays in the same flash, which the panel says plainly |
