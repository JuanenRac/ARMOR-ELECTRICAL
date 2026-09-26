# Safety: what this project does and does not do about mains voltage

Mains (230 V AC) and the DC of a battery bank can kill and start fires. This document says what the project's design assumes; it is not a design of an installation and it does not replace the regulations of the country or a qualified installer.

## Principles

1. **Measure with modules made for it.** The PZEM meters are sold as finished, enclosed modules; a node should read them over their serial link (which is isolated in the module) and never carry mains on its own board. Where a current transformer or a shunt is used, it goes on the circuit inside the panel, wired by someone qualified.
2. **Reading first, switching much later.** A node that only reads cannot make a fault. Everything that switches (an electronic breaker, a contactor, a source transfer) is a separate step, designed with the equipment in front of it and tried on a bench with a lamp behind an isolating transformer before it touches an installation.
3. **Software is the second layer, never the only one.** A transfer between two sources (the grid and an inverter's output, a generator) must have a **mechanical interlock** between its two contactors, contactors and breakers rated and certified for the load and the fault current, and the ordinary protections of the installation (breakers, residual-current devices, surge protection). The rules in `core/interlock.hpp` are the software layer on top.
4. **Fail to open.** Every controller here answers a fault, a lost feedback or the switching being off with all coils open. Whether "open" is a safe state for what is behind it (a freezer, a pump, a medical device) is a decision about the installation.
5. **Off by default, at three independent places.** Switching is disabled unless the firmware was built and set up to allow it (the message says so: `switching_enabled`), unless the server was started with `ARMOR_ELECTRICAL_SWITCHING=1`, and unless the broker's ACL was opened for that node (`scripts/mqtt_identity.sh electrical-switching`). Studio never shows a control for something that cannot be switched. Today no firmware image contains the commands at all.
5b. **A close cannot be replayed.** Closing is an `arm` and then a close carrying the one-time token the node gave; the commands are never retained, and an old one finds no token to match.
6. **Confirm with what you see.** A switch's state in the message is what the auxiliary contact shows, not what was asked.
7. **The drawing checks are a guide.** The Electrical Designer's checks (breaker against current, cable against breaker, two sources on one line) are rules of thumb for reviewing a drawing.

## What has and has not been done

Nothing in this project has been connected to a meter, a contactor or an installation. The tests use stand-ins. The first time a real meter is read, and above all the first time anything is switched, is done by a person, on a bench, with the precautions above.
