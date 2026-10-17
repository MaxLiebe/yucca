# Hardware test: Switch and Switch 2

Regression script for the Switch target (YUCCA as a Switch Pro Controller). Run it once per console (`target=switch` or `target=switch2`) and once per controller you want to mark verified, on a freshly flashed board.

Record: YUCCA commit, console and system version, controller, ESP32 board. Watch the serial log (`idf.py monitor`, 115200). The protocol and the open questions are in `docs/platforms/switch.md`.

Before the first run, turn on **System Settings > Controllers and Sensors > Pro Controller Wired Communication** only if you also test over USB (not needed here). On Switch 2, vibration for third-party Pro-compatible controllers may be off in settings; turn it on for step 8.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1 | Erase flash, flash, set the console to Switch (web page, or the Select + Home menu: LED 3; Switch 2 is LED 4), boot | Log: `First run: pair the controller now; then open Change Grip/Order on the Switch` | |
| 2 | Put the controller in pairing mode (see `docs/controllers.md`) | Log: `Gamepad ready: ...`, then `No Switch paired yet: opening the sync window` | |
| 3 | On the console open **Controllers > Change Grip/Order** (Switch 2: "Change Grip and Order") within 30 s | Log: `Incoming ACL from console ...`, a run of `Subcommand 0x..` lines, then `paired with the ESP32 as slave; reconnecting as master` and `Switch connected` again | |
| 4 | Press L + R on the controller | The console registers a Pro Controller; the controller shows player LED 1 | |
| 5 | HOME menu and System Settings: d-pad, sticks, A, B, X, Y, +, -, HOME, Capture | Menus respond; Capture takes a screenshot | |
| 6 | **Controllers > Test Input Devices > Test Controller Buttons**: every button, both sticks, stick clicks, L/R, ZL/ZR | Each lights up as in `docs/platforms/switch.md` section 3; sticks reach the circle edge in all directions | |
| 7 | **Calibrate Control Sticks** screen | Sticks rest in the centre and reach the outer ring | |
| 8 | A game or menu that rumbles (e.g. the grip screen's "vibration" test, Mario Kart 8 crash) | Controller rumbles while the console asks, stops after | |
| 9 | Play a fast game (e.g. Smash Bros., Mario Kart 8) for 10 minutes | No disconnects, no stuck inputs; note any lag you feel | |
| 10 | Turn the controller off | Inputs release; the console keeps YUCCA connected (log: `Keeping the Switch link`) | |
| 11 | Turn it back on | Controller reconnects; input works without visiting the grip screen | |
| 12 | Power-cycle the ESP32, then press Home on the controller | Controller reconnects, then the console connects without the grip screen | |
| 13 | Put the console to sleep | Log: `Switch asked us to disconnect` | |
| 14 | Press a controller button | Switch 1: the console wakes and YUCCA connects. Switch 2: a Switch 1 Pro Controller cannot wake it (known); wake it with the power button, then press a button and YUCCA connects | |
| 15 | Motion: open a game with motion controls | Expected: no motion (not emulated in v1); the game must not see the controller as broken | |
| 16 | Hold BOOT for 5 s | Forgets the console and controllers; steps 1-4 work again (remove the old "Pro Controller" entry on the console first) | |

Unknowns worth capturing in the report (see `docs/platforms/switch.md` sections 8 and 9): whether step 3 passes on the first try, every `Subcommand 0x..` id the log shows (Switch 2 may send new ones), and whether the controller link stays smooth while YUCCA is connected as the console's slave during step 3.

File the result with the "Hardware test report" issue template.
