# Wii

Status: implemented (`components/target_wii`). Last checked: 2026-10-17.

## Summary

The Wii talks to Wii Remotes over Bluetooth Classic HID. YUCCA is a Wii Remote (`Nintendo RVL-CNT-01`, class of device `0x002504`) with a Classic Controller plugged in, using [WiimoteAnything](../../components/wiimote_anything/README.md). vWii on a Wii U behaves the same.

## Transport and pairing

- L2CAP PSM `0x11` (control) and `0x13` (interrupt), report headers `0xA2` out / `0xA1` in.
- Sync: 20 s window, the ESP32 is connectable and limited-discoverable. The user presses the red SYNC button on the console. Legacy PIN pairing: the Wii's BD_ADDR reversed. The Wii has no Secure Simple Pairing.
- After sync the remote pages the Wii itself to reconnect (button press). The Wii is recognised as: the console synced before, a device with class of device `0x000448`, or anything connecting during the sync window. Its connection and PIN requests are hidden from Bluepad32.

## Sharing the radio with the controller

- The original ESP32 radio stalls when it is slave on one link and master on another, so YUCCA stays master on both. It pages the Wii itself with role switching refused. If the Wii paged us (sync), YUCCA hangs up and pages back as master 1 s later.
- Once a controller is ready (never while it is still pairing), YUCCA asks to be its master too, with sniff mode off and a short poll interval (`wiimote_btstack_tune_link`).
- Controller first, then the Wii. Without a controller the Wii link is released; the pairing is kept.
- A controller's incoming L2CAP channels reach Bluepad32 only once the link is authenticated and encrypted (Switch-mode pads depend on that).

## Reconnect behaviour

- The Wii drops every remote when a game or homebrew app starts. YUCCA then pages it back to back (first page after 200 ms, 100 ms gaps) for up to 60 s, or 15 s when the Wii reported "powered off" (reason `0x15`, which some IOS reloads also report).
- Any controller button press retries a page, at most once a second.
- Controller lost while connected as master: the Wii link is kept with all inputs released (some games hang on their "reconnect" screen otherwise).

## Reports and mapping

Classic Controller, 6-byte report, by position by default:

| Controller | Classic Controller | Wii Remote (mirrored, on by default) |
| --- | --- | --- |
| East / South / North / West | a / b / x / y | A / B / 2 / 1 |
| D-pad | D-pad | D-pad |
| L1 / R1 | ZL / ZR | |
| L2 / R2 (analog) | L / R analog, click at 85 % | |
| Select / Start | − / + | − / + |
| Home | HOME | HOME (always) |
| Sticks | Sticks | |

Label layout (setting `face_layout` = 1) maps an Xbox-style controller by label instead (A → a).

Feedback: the Wii's player LEDs go to the controller's player LEDs (or a light bar colour per player); the Wii's rumble bit runs both motors (heavy full, light half). Battery: controller % maps to `0x20..0xC8`; an empty controller reports `0x10` (the Wii's low-battery warning).

## Quirks and risks

- Sync must happen within a few metres; range afterwards is the ESP32 module's.
- No IR pointer (no camera); pointer games need a sensor-bar-free alternative (future: gyro to IR emulation).

## Sources

- WiimoteAnything README ("What a Wii will accept"), from the Wiibrew Wiimote, extension and MotionPlus notes.
- wii-gamepad-adapter README ("How the sharing works"), behaviour tested on hardware by Max.

## Fixtures

None yet. WiimoteAnything's `tests/test_btstack_backend.c` covers a Wii syncing, reconnecting and a gamepad sharing the stack against fake BTstack calls.
