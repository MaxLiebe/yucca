# Hardware test: Wii U

Regression script for the Wii U target. Run it on a Wii U in Wii U mode (not vWii; for vWii use `wii.md`) with a freshly flashed board, once per controller you want to mark verified.

Record: YUCCA commit, Wii U system version, controller, ESP32 board, and `wiiu_mode` (0 Pro Controller, 1 Wii Remote + Classic). Watch the serial log (`idf.py monitor`, 115200).

The Wii U needs Secure Simple Pairing off, so in Wii U mode new controllers pair with the legacy PIN method. Step 2 checks that; if a controller will not pair, note it, then pair it once with the console set to Wii and switch back (bonds are kept).

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1 | Erase flash, flash, set the console to Wii U (web page, or the Select + Home menu: LED 2 lit), boot | Log: `Secure Simple Pairing off` and `First run: pair the controller now; the Wii U sync window opens afterwards` | |
| 2 | Put the controller in pairing mode (see `docs/controllers.md`) | Log: `Gamepad ready: ...`, then `No Wii U paired yet: opening the sync window` | |
| 3 | Press SYNC on the Wii U (console front, or HOME Menu > Controller Settings > Pair) within 20 s | Log: `Wii U connected` (possibly after `reconnecting as master`). Controller shows a player LED | |
| 4 | Wii U Menu and HOME Menu: d-pad, A, B, HOME | Menus respond | |
| 5 | In a Pro Controller game (e.g. Mario Kart 8, Smash Bros. for Wii U), test every button, both sticks, stick clicks, L/R, ZL/ZR | Each reaches the game as in `docs/platforms/wiiu.md`; the game shows a Pro Controller | |
| 6 | Controller Settings or a game that shows battery | Battery level is plausible (0-4 bars) | |
| 7 | Something that rumbles | Controller rumbles while the Wii U asks, stops after | |
| 8 | Close the game, start another | YUCCA comes back on its own if the Wii U drops it | |
| 9 | Power-cycle the ESP32, then press PS/Home on the controller | Controller reconnects, then the Wii U connects without SYNC | |
| 10 | Wii U off and on, then press a controller button | YUCCA reconnects to the Wii U | |
| 11 | Set `wiiu_mode` to 1, re-sync, open a game that takes the Classic Controller (e.g. a Wii U Virtual Console title) | Works as a Wii Remote + Classic Controller | |
| 12 | Hold BOOT for 5 s | Forgets the console and controllers; steps 1-3 work again | |

Unknowns worth capturing in the report (see `docs/platforms/wiiu.md` section 5): the Wii U's class of device (logged on first sync as `Incoming ACL from console ... (cod 0x......)`), which data report mode it asks for, and anything the log shows as rejected.

File the result with the "Hardware test report" issue template.
