# Hardware test: Wii

Regression script for the Wii target. Run it on a Wii (or vWii) with a freshly flashed board, once per controller you want to mark verified.

Record: YUCCA commit, Wii model and system menu version, controller, ESP32 board. Watch the serial log (`idf.py monitor`, 115200).

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1 | Erase flash (`idf.py erase-flash`), flash, boot | Log: `First run: pair the controller now` and `Looking for a controller` | |
| 2 | Put the controller in pairing mode (see `docs/controllers.md`) | Log: `Gamepad ready: ...`, then `No Wii paired yet: opening the sync window` | |
| 3 | Press the red SYNC button on the Wii within 20 s | Log: `Wii connected` (possibly after `reconnecting as master`). Controller shows player 1 | |
| 4 | Wii Menu: press A, d-pad, HOME | Menu responds (core buttons mirrored) | |
| 5 | In a Classic Controller game (e.g. Mario Kart Wii, Smash Bros. Brawl), test every button, both sticks, both triggers | Each reaches the game as in `docs/platforms/wii.md` | |
| 6 | Something that rumbles | Controller rumbles while the Wii asks, stops after | |
| 7 | Start a game from the Wii Menu (the Wii drops all remotes) | Log: `Wii disconnected (reason ...)` then `Wii connected` within a few seconds, no button press needed | |
| 8 | Switch the controller off for 30 s mid-game, then on | Wii link stays up (game sees an idle remote), controller comes back, inputs work | |
| 9 | Power-cycle the ESP32, then press PS/Home on the controller | Controller reconnects, then the Wii connects without SYNC | |
| 10 | Wii off and on (from the Wii Menu), then press a controller button | YUCCA reconnects to the Wii | |
| 11 | Hold Select + Start for 3 s | Log: `Sync: press the red SYNC button`; re-sync works | |
| 12 | Hold BOOT for 5 s | Log: `Forgetting the console and all controllers`; step 1 to 3 works again | |
| 13 | Leave idle 10 minutes with the Wii Menu open, then use | Still connected, no stuck inputs | |

File the result with the "Hardware test report" issue template.
