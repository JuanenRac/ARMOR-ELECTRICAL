# Switching: the rules of the controller

`core/interlock.hpp` holds the software rules for switching two sources onto one line, kept apart from any hardware so they can be tested to exhaustion. **Nothing drives any hardware yet.**

## What a transfer controller does

Two sources, `A` and `B` (for example the grid and an inverter's output), each behind a contactor whose auxiliary contact tells the node whether it is closed.

- `arm(now)`: the operator's first step. The next request to close is accepted for ten seconds (`arm_window_ms`), and then only once.
- `request(source, now)`: asks for `A`, `B` or none. **None (open) is always accepted and immediate.** A request to close is refused when the switching is not allowed (`kDisabled`), a fault is latched (`kFault`) or the node was not armed (`kNotArmed`).
- `tick(feedback, now)`: called often with the auxiliary contacts as they are.
- `coils()`: what to drive. Never both at once; a coil is energised only after both contactors were confirmed open for the whole `dead_time_ms` (two seconds by default). Going from one source to the other opens the first, waits for its contact, waits the dead time, then closes the second (break before make).
- **Faults** (all open, latched): a contactor that does not show closed within `confirm_timeout_ms` of being told (`kDidNotClose`), one that is closed when nobody asked or does not open when told (`kDidNotOpen`), both contacts closed (`kBothClosed`). A fault never clears itself: `acknowledge` works only with both contactors confirmed open.

## What the tests check

Scripted scenarios (switching off, arming, a transfer both ways, a welded contactor, one that will not close, both closed, one that falls out, something closed by itself) and 135,000 random steps with contactors of random speed that weld, stick open and move by themselves: never two coils, nothing energised with the switching off, every energised coil preceded by the whole dead time with both contacts open, a fault always means all coils open, and a request to open drops every coil at the next tick.

## What it is not

Not a protection relay, not a certified safety function, and not a substitute for a mechanical interlock. The contactors are chosen and wired by an installer; this only decides when to ask them to move.
