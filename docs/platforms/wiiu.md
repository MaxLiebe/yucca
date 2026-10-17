# Wii U (Pro Controller emulation)

Status: research notes, 2026-10-17. Written in our own words from the sources listed at the end. No GPL code was copied; tables restate facts (bit positions, byte values) that several independent sources agree on. Confidence tags: **[confirmed]** = two or more independent sources agree; **[single source]**; **[inferred]** = our reasoning from related facts; **[unknown]** = must be measured on hardware.

## 1. Summary

- The Wii U Pro Controller (WUP-005) is, on the wire, a Wii Remote with no IR camera, no accelerometer, no speaker, no EEPROM, and a permanently "plugged in" extension whose identifier is `00 00 A4 20 01 20` (read at `0xA400FA`) **[confirmed: Linux hid-wiimote, SDL, Bluepad32, Wiiubrew]**. Same L2CAP PSMs (`0x11`/`0x13`), same `0xA2`/`0xA1` report headers, same output reports (`0x10`–`0x1A`), same status/read/ack reports. The extension payload is 11 bytes: four 12-bit sticks, 16 active-low button bits, then a byte with battery level, charge/USB flags and the two stick clicks.
- Its Bluetooth name is `Nintendo RVL-CNT-01-UC` and its HID product ID is `0x0330` (same PID as the RVL-CNT-01-TR "Wii Remote Plus") **[confirmed]**.
- Wii U software (padscore/WPAD, KPAD on top) knows the Pro Controller as extension type 31 ("URCC", `WPAD_EXT_PRO_CONTROLLER`), data format 22 (`WPAD_FMT_PRO_CONTROLLER`) and also supports Wii Remote core, Nunchuk, Classic, MotionPlus combos **[confirmed: wut headers, Cemu HLE]**. Each game opts in with `WPADEnableURCC()` / `WPADEnableWiiRemote()`, so which controller works is per game.
- Biggest blocker: **the Wii U's Bluetooth stack attempts Secure Simple Pairing; if the emulated controller has SSP enabled, pairing fails** (BluuBomb author, found by hooking IOSU) **[single source, but specific and from a working Wii U emulation project]**. YUCCA's BTstack instance has SSP on (BTstack default, and Bluepad32 auto-accepts SSP). On a Wii, this did not matter because the Wii has no SSP. For the Wii U we must turn host SSP support off, which is controller-wide and affects how gamepads pair.
- Everything else (legacy PIN = console BD_ADDR reversed, limited-discoverable sync, console pages the controller during sync, controller pages console on reconnect, extension init `0x55`/`0x00` or encrypted) is the same machinery wiimote_anything already implements.

## 2. Pairing with a Wii U

### 2.1 User flow (SYNC)
Nintendo's support article: on the Wii U Menu (or HOME Menu) press SYNC on the console; the TV shows the pairing screen; press SYNC on the controller; the controller's player LED lights when done. Up to 4 Pro Controllers / Wii Remotes in total for player slots (Nintendo phrases it as "up to six Wii Remotes and/or four Pro Controllers" supported) **[confirmed, Nintendo]**. Timeout of the console's window is not documented **[unknown]**; YUCCA's 20 s discoverable window is the controller side and should be fine.

The Pro Controller has no "1+2 temporary sync"; it must be paired once via SYNC and the link key kept for one-touch reconnection **[single source: WiiUBrew Pro Controller]**.

### 2.2 Baseband / LMP details

| Item | Wii Remote (what YUCCA does now) | Wii U Pro Controller | Notes |
| --- | --- | --- | --- |
| Local name | `Nintendo RVL-CNT-01` | `Nintendo RVL-CNT-01-UC` | Confirmed by `hcitool info` (WiiUBrew), BlueZ `wiimote.c` and Linux `hid-wiimote-core.c` name tables. Dolphin's comment: Wii software checks only the name prefix `Nintendo RVL-CNT`. Whether the Wii U checks the suffix **[unknown]**. |
| Class of device | `0x002504` (limited-discoverable, peripheral, joystick) | `0x000508` (peripheral, minor = gamepad) as read by `hcitool info` while connected | A third-party "RVL-CNT-01-UC" clone reported `0x002504`. Recommend `0x002508` during the sync window (limited-discoverable bit set) and `0x000508` otherwise **[inferred]**. Whether the Wii U filters on CoD **[unknown]**, the clone suggests it does not care much. |
| Discovery | Limited inquiry (LIAC) | Clone only showed up under LIAC inquiry | Keep `LIAC` discoverable during sync **[inferred]**. |
| BD_ADDR OUI | Nintendo | Nintendo `40:F4:07` on the sample | Not believed to be checked **[unknown]**. |
| SDP / HID IDs | VID `0x057E`, PID `0x0306` (or `0x0330`) | VID `0x057E`, PID `0x0330` | Bluepad32 uses PID `0x0330` + status "extension present" + ext id to tell Pro from Remote Plus. Report descriptor of a real Pro is believed identical to the Remote's **[unknown, capture it]**. |
| LMP features (sample) | — | `BC 02 04 38 08 00 00 00`, LMP 2.0, CSR chip | Byte 6 bit 3 ("Secure Simple Pairing") is 0: **the real Pro does not support SSP** **[inferred from the feature bitmask on WiiUBrew]**. |
| Pairing | Legacy PIN, PIN = host BD_ADDR as 6 raw bytes, little-endian order (i.e. the printed address reversed) | Same | BlueZ's wiimote plugin treats `-UC` exactly like a Remote and returns the adapter address as PIN. **[confirmed for PC hosts; for the Wii U console inferred]**: BluuBomb paired a BlueZ Wiimote emulator to a Wii U once SSP was disabled, which leaves legacy PIN as the only path. |
| SSP | Wii has none | Wii U stack tries SSP; pairing fails if the controller side offers SSP | BluuBomb WRITEUP + README ("make sure `hciconfig hci0 sspmode` says Disabled") **[single source]**. Why it fails (IO-cap mismatch, key type check) **[unknown]**. |

### 2.3 Who connects
- **During sync** the console inquires and pages the controller; the controller only needs to be connectable + limited-discoverable (the rnconrad WiimoteEmulator README, used unmodified by BluuBomb on the Wii U, "listens for incoming connections, similar to syncing a real Wiimote") **[confirmed for Wii, single source for Wii U]**.
- **On reconnect** the controller pages the console: a Pro Controller powers down after idle and reconnects when a button is pressed; BluuBomb reconnects with `bluubomb <wii-u-bdaddr>` (the emulator pages the console) **[confirmed]**. The console then authenticates with the stored link key, as on the Wii. Whether the Wii U ever pages a bonded controller itself (e.g. on power-on) **[unknown]**; assume not.
- Console CoD of a Wii U (YUCCA's `is_console()` recognizes the Wii by `0x000448`) **[unknown]**: log it on first sync; until then rely on "stored console address" and "any non-peripheral host during sync".

## 3. Pro Controller protocol (differences from a Wii Remote)

### 3.1 Extension registers (`0xA4xxxx`, I2C slave `0x52` on a real Remote)

| Address | Value | Notes |
| --- | --- | --- |
| `0xA400FA`..`FF` | `00 00 A4 20 01 20` | Linux matches bytes 4–5 = `01 20`; Bluepad32 comments the full 6 bytes; WiiUBrew reads 4 bytes at `0xA400FC` = `A4 20 01 20`; SDL reads 2 bytes at `0xA400FE` = `0x0120`. **[confirmed]** |
| `0xA400F0` / `0xA400FB` | host writes `0x55` then `0x00` to disable encryption | Same as other extensions. WiiUBrew: "reports are encrypted by default" until this init. Whether Wii U's WPAD uses the unencrypted init or the `0xAA` + key path **[unknown]**; wiimote_anything already supports both. |
| `0xA40020`..`3F` (calibration) | **[unknown]** | Linux: "we don't know any calibration data in the EEPROM", auto-zeroes on first report; SDL uses fixed min/max. WPAD exposes raw sticks in `[-2048, 2047]` (wut `WPADStatusProController`), i.e. raw minus `0x800`, suggesting no per-unit calibration in WPAD. KPAD may still read the block; capture a real dump. |
| `0xA600FA` (MotionPlus probe) | **[unknown]** | Linux: "the device reports MP support, which didn't work", so the driver disables MP probing for this device. SDL never probes MP on it. Safest emulation: answer the read with error (`0x07`, "not mapped") like a Remote without MP, or mirror a captured real answer. |
| EEPROM (`0x0000`–`0x16FF`) | none | WiiUBrew: reading it makes a real Pro Controller disconnect. Wii U WPAD therefore cannot depend on it for URCC. YUCCA can keep answering (harmless) **[inferred]**. |

### 3.2 Extension payload (11 bytes, offsets from the first extension byte)

All buttons are active-low (bit = 0 while pressed). Sticks are 12-bit little-endian, high nibble of the upper byte 0.

| Byte | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | LX[7:0] | | | | | | | |
| 1 | 0 | 0 | 0 | 0 | LX[11:8] | | | |
| 2 | RX[7:0] | | | | | | | |
| 3 | 0 | 0 | 0 | 0 | RX[11:8] | | | |
| 4 | LY[7:0] | | | | | | | |
| 5 | 0 | 0 | 0 | 0 | LY[11:8] | | | |
| 6 | RY[7:0] | | | | | | | |
| 7 | 0 | 0 | 0 | 0 | RY[11:8] | | | |
| 8 | D-Right | D-Down | L | Minus | Home | Plus | R | 1 (reserved) |
| 9 | ZL | B | Y | A | X | ZR | D-Left | D-Up |
| 10 | see note (1) | Battery[2] | Battery[1] | Battery[0] | flag A (2) | flag B (2) | L-stick click | R-stick click |

Order is LX, RX, LY, RY (not LX, LY, RX, RY) **[confirmed: Linux, SDL]**. Bytes 8–9 use exactly the Classic Controller's button bit positions (Classic bytes 4–5), which is why SDL parses both with one table **[confirmed]**. WPAD's `WPADProButton` masks are the bitwise inverse of `(byte8 << 8) | byte9`, with stick clicks at `0x10000` (R, byte 10 bit 0) and `0x20000` (L, byte 10 bit 1); WPAD calls bit `0x100` (byte 8 bit 0) "reserved" **[confirmed: wut, Cemu, Linux layouts agree]**.

Stick direction: Linux maps X as `raw - 0x800` and Y as `0x800 - raw`, so up/right... note the Y inversion is the Linux input convention (evdev Y grows downward); on the wire Y grows **up**, X grows right, like the Classic Controller **[confirmed by the formula]**. Centre `0x800`. Real travel is roughly ±1000 counts (SDL assumes 1000..3000; Linux exposes ±0x400) **[confirmed, approximate]**. Recommend mapping ±1.0 to `0x800 ± 0x480` and clamping to `0x000..0xFFF` **[inferred]**.

Notes on byte 10:
1. Bit 7: Linux draws it as a constant `1`; SDL computes `level = byte10 >> 4` and says it never saw a value above 4, which implies bit 7 = 0 on that unit. **Conflict [unknown]**. Capture from a real Pro via Bluepad32 before choosing; default 0 is what SDL observed.
2. Bits 3 and 2: both are "0 = true". Linux labels bit 3 USB-connected and bit 2 charging; SDL labels bit 3 charging and bit 2 plugged-in; WiiUBrew only says "Wired and Charging read 0 when true". **Polarity agreed, assignment conflicting**. Send both bits = 1 (on battery, not charging), which all three readings agree means "wireless, not charging" **[confirmed for that value]**. WPAD reports these as `charging` and `wired`.
3. Battery: 3 bits, 0 (empty) … 4 (full); values above 4 not observed **[confirmed: Linux "000 to 100", SDL]**. SDL's observation: 4, 3, 2 last about 20 h each, 1 about 6 h, 0 about 2 h.

Bytes 11+ (unused tail of 16/19/21-byte extension fields): **[unknown]**; send `0x00` unless a capture shows otherwise.

### 3.3 Reports and modes

| Topic | Wii Remote | Pro Controller |
| --- | --- | --- |
| Data report mode | host chooses `0x30`–`0x3F` | Linux and Bluepad32 request `0x34` (2 core-button bytes + 19 ext bytes); SDL requests `0x3D` (21 ext bytes). Both work on a real Pro. A clone was reported to send nothing until `0x3D` was chosen. Which mode Wii U WPAD picks for format 22 **[unknown]**; implement both (wiimote_anything already does). |
| Core button bytes (BB BB) | buttons | **[unknown]**; probably idle (no buttons). Send `0x00 0x00` (core buttons are active-high) plus the rumble/… conventions as now. |
| Accelerometer / IR | yes | none. If a host asks for an accel or IR mode, send idle values **[inferred]**. |
| Status report `0x20` | LF byte: bit 1 = extension connected | Bit 1 is set (extension always present). Bluepad32 relies on this. Status battery byte: Wii U tracks Pro battery from byte 10 (SDL: "Wii U has separate battery level tracking"); fill status byte too (e.g. `0xC0`) **[inferred]**. |
| Extension change | `0x20` unsolicited on plug/unplug | Never changes. Do not send unsolicited extension-change status except once at connect if a real one does **[unknown]**. |
| Rumble | bit 0 of every output report | Same, one motor (Linux treats any magnitude as on/off) **[confirmed]**. |
| LEDs | `0x11`, bits 4–7 = LED 1–4 | Same, 4 player LEDs **[confirmed]**. |
| Speaker `0x14/0x18/0x19` | yes | No speaker **[confirmed: hardware]**. Accept and ignore, as now. |
| Power button | yes | Pro has a power/HOME; HOME is byte 8 bit 3. |

## 4. Wii Remote + Classic Controller on the Wii U (fallback)

- WPAD in Wii U mode exposes core, Nunchuk (`1`), Classic (`2`), MotionPlus (`5`, `6`, `7`) and URCC (`31`) **[confirmed: wut `WPADExtensionType`; Cemu's HLE uses the same set]**. So Wii U games that call `WPADEnableWiiRemote(TRUE)` and accept the Classic format will take YUCCA's current Wii Remote + Classic Controller (Pro) identity unchanged at the protocol level.
- Game support varies: each title chooses controllers. Many Wii U games support the Pro Controller but not the Classic Controller; some support Wii Remote (+ Nunchuk) only. Prefer Pro mode; keep Classic mode as fallback **[general knowledge, not exhaustively verified]**.
- Wii U Menu: forum reports say Wii Remote (pointer), Classic Controller (Pro) and Pro Controller can navigate the main menu, but System Settings, data management, updates and first-time setup need the GamePad **[single source: user forum, low confidence]**.
- Same SSP problem applies: the pairing blocker is in the Bluetooth layer, not in the controller identity.

## 5. Known quirks and implementation notes

1. **SSP must be off** on the ESP32 when pairing with a Wii U (section 2.2). In BTstack: `gap_ssp_set_enable(0)` before `hci_power_control(HCI_POWER_ON)`; in Bluedroid: `CONFIG_BT_SSP_ENABLED=n`. Host SSP support is advertised per controller, not per link, so it also forces legacy PIN on gamepads that pair while it is off. Already-bonded gamepads should still reconnect with their stored keys **[inferred, test]**; DualSense/Switch-style pads that only do SSP may not pair new. Spec note: older Core specs forbid turning SSP back off after enabling without a reset, so it must be set at boot **[inferred]**.
2. Wii U may check the name prefix only; using the exact `Nintendo RVL-CNT-01-UC` is free, so use it.
3. `0x0330` PID in the SDP record (wiimote_anything's descriptor was dumped from an RVL-CNT-01; the PID field must change for Pro mode).
4. MotionPlus probing must not report a working MotionPlus in Pro mode.
5. Calibration block, byte 10 bit 7, unused tail bytes, Wii U console CoD, which data mode Wii U WPAD requests, encryption choice: all **[unknown]**, and YUCCA can measure most of them itself: Bluepad32 (already in the build) hosts a real Wii U Pro Controller, so a debug build can dump `0xA400FA`, `0xA40020`–`0x3F`, `0xA600FA`, the status report and raw `0x34` frames. Logging the Wii U's own `0xA2` traffic during a first sync (report `0x12` mode, `0x16` writes to `0xA400F0/FB/40`, reads) answers the console side.
6. Dolphin: Wii-only, does not emulate a Pro Controller; useful only for the shared Wiimote/extension/cipher behaviour wiimote_anything already follows. Its real-Wiimote code accepts PID `0x0306`/`0x0330` and names starting `Nintendo RVL-CNT`.
7. Cemu: emulates Wii U software at HLE level (padscore/WPAD), so it gives the WPAD-side view (types, formats, button masks) but its real-Wiimote provider does not parse a Pro extension; no wire-level Pro emulation.
8. Third-party "RVL-CNT-01-UC" clones exist with CoD `0x002504` and SSP-capable radios; reports of them working on a Wii U suggest the console tolerates CoD and name variations, but they are anecdotal **[unknown]**.

## 6. Changes needed in `components/wiimote_anything`

| Area | Change |
| --- | --- |
| `include/wiimote_ids.h` | Add `WIIMOTE_EXT_WIIU_PRO` (or a separate `wiimote_device_t` profile: `WIIMOTE_DEVICE_REMOTE`, `WIIMOTE_DEVICE_WIIU_PRO`). Add buttons `..._PRO_LSTICK`, `..._PRO_RSTICK` (or reuse `CLASSIC_*` for the 15 shared buttons and add two). Add 12-bit axis handling for LX/LY/RX/RY (can reuse `WIIMOTE_AXIS_CLASSIC_LX..RY`; triggers are digital on the Pro, so LT/RT map to L/R buttons only). |
| `include/wiimote.h` config | `wiimote_config_t`: profile field; `disable_ssp` (or `host_console = WII / WII_U`) flag; battery/charging/wired setters (`wiimote_set_pro_power(level0to4, charging, wired)`). |
| `src/wiimote_ext.c` | New id `00 00 A4 20 01 20`; register image with (captured or zeroed) calibration; payload packer per table 3.2 (sticks LE 12-bit, bytes 8–9 Classic bit positions, byte 10 battery/flags/clicks, reserved bit byte 8 bit 0 = 1); pad unused tail bytes; keep cipher path. Disallow runtime switching to/from Pro (it is not an extension that can be unplugged; changing identity needs a re-sync because the name/PID differ). |
| `src/wiimote_proto.c` | Pro profile: no IR, no accel data (idle), status LF bit 1 always set, MotionPlus register `0xA600xx` returns "not mapped", optionally EEPROM reads keep working. Core button bytes idle. Continue supporting all modes incl. `0x34` and `0x3D`. |
| BT backends (`wiimote_bt_btstack.inc`, `wiimote_bt_bluedroid.inc`) | Local name `Nintendo RVL-CNT-01-UC`; CoD `0x002508` during sync / `0x000508` after; SDP PID `0x0330`; option to call `gap_ssp_set_enable(0)` before power-on (BTstack) / disable SSP (Bluedroid); log the Wii U's CoD and extend `is_console()` once known (today it keys on Wii `0x000448`). PIN logic unchanged. |
| Bluepad32 coexistence | Document that Wii U mode turns SSP off for the whole radio; test which gamepads still pair/reconnect. |
| `tests/test_proto.c` | Golden vectors: ext id read, payload packing vs the Linux/SDL decoders (decode the way they do, as the existing tests do for Classic), byte 10 values, MP probe error. |
| `src/Wiimote.h` wrapper, examples, README | `begin(WIIMOTE_EXT_WIIU_PRO)`, a "Wii U" section (SSP note, per-game support). |

## 7. Sources and licences

| Source | What it gave | Licence |
| --- | --- | --- |
| Linux `drivers/hid/hid-wiimote-modules.c` (`wiimod_pro`), `hid-wiimote-core.c` (ext id, name table, DRM selection), `hid-wiimote.h` — https://github.com/torvalds/linux/blob/master/drivers/hid/hid-wiimote-modules.c ; original patch https://lkml.iu.edu/hypermail/linux/kernel/1306.1/03773.html | Payload layout, ext id `01 20`, `0x34` mode, rumble, MP disabled, name | GPL-2.0-or-later (facts only used) |
| SDL `src/joystick/hidapi/SDL_hidapi_wii.c` — https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_wii.c | ext id `0x0120`, `0x3D` mode, byte 10 decoding, battery duration observations, stick range | zlib |
| Bluepad32 `uni_hid_parser_wii.c` (in-tree at `external/bluepad32/...`) — https://github.com/ricardoquesada/bluepad32 | full id `00 00 a4 20 01 20`, PID `0x0330`, status ext bit, `0x34` | Apache-2.0 (BTstack itself: BlueKitchen licence) |
| WiiUBrew "Pro Controller" — https://wiiubrew.org/wiki/Pro_Controller | name, CoD `0x000508`, LMP features, `0xA4200120`, EEPROM read disconnects, encrypted by default, 1-touch reconnect | CC BY-SA (wiki text) |
| WiiBrew Wiimote / Extension Controllers pages — https://wiibrew.org/wiki/Wiimote , https://wiibrew.org/wiki/Wiimote/Extension_Controllers | base protocol (already implemented). The Pro subpage could not be fetched during this research | CC BY-SA / GFDL (wiki text) |
| wut `include/padscore/wpad.h` — https://github.com/devkitPro/wut , docs https://wut.devkitpro.org/wpad_8h.html | WPAD ext types (URCC = 31), format 22, Pro button masks, stick range, charging/wired, `WPADEnableURCC` / `WPADEnableWiiRemote` | zlib |
| Cemu `src/input/emulated/WPADController.h`, `src/input/api/Wiimote/*` — https://github.com/cemu-project/Cemu | same WPAD constants; real-Wiimote provider lacks Pro | MPL-2.0 |
| Dolphin `Source/Core/Core/HW/WiimoteReal/WiimoteReal.cpp` — https://github.com/dolphin-emu/dolphin | name prefix check, PIDs; WiimoteEmu (already followed for cipher) | GPL-2.0-or-later |
| BluuBomb README and WRITEUP — https://github.com/GaryOderNichts/bluubomb , https://github.com/GaryOderNichts/bluubomb/blob/master/WRITEUP.md | Wii U pairs an emulated Wiimote only with SSP disabled; IOS-PAD / `btrm` architecture; reconnect by paging | no licence file found at repo root (treat as all rights reserved; facts only) |
| rnconrad WiimoteEmulator — https://github.com/rnconrad/WiimoteEmulator | Linux Wiimote emulator used by BluuBomb; listens during sync, pages on reconnect | licence not determined (no LICENSE/COPYING found) |
| BlueZ `plugins/wiimote.c` — https://git.stg.centos.org/source-git/bluez/blob/c8/f/plugins/wiimote.c | `-UC` treated as Wiimote, PIN = host address bytes | GPL-2.0-or-later |
| BlueZ issue #94 (third-party Pro clone) — https://github.com/bluez/bluez/issues/94 ; Nintendont issue #641 — https://github.com/FIX94/Nintendont/issues/641 | clone CoD `0x002504`, LIAC only, `0x3D` vs `0x34`, reconnect on button | issue text |
| Nintendo support: sync Pro Controller — https://en-americas-support.nintendo.com/app/answers/detail/a_id/1561 | user sync flow, controller limits | — |
| Nintendo Life forum: Wii U Menu without GamePad — https://www.nintendolife.com/forums/wii-u/please_allow_wii_u_settings_to_be_changed_without_a_gamepad | which controllers navigate the menu (anecdotal) | — |

No public microcontroller project that emulates a Wii U Pro Controller **towards a Wii U** was found (searches turned up hosts that read a real Pro: Bluepad32, BlueRetro, SDL, Linux). BluuBomb + WiimoteEmulator is the only published emulator known to pair with a Wii U, and it presents a Wii Remote, not a Pro Controller.
