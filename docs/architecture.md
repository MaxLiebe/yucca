# Architecture

```
controller --BT--> InputDriver --GamepadState--> Mapper --> Target --BT--> console
           <------ feedback (rumble, LEDs) <------------------------
```

One original ESP32, one radio, one BTstack instance shared by the input side (Bluepad32) and the target side. The ESP32 stays master on every link (the radio stalls when it is slave on one link and master on another).

## Interfaces (`components/yucca_core`)

- **`GamepadState`**: canonical, controller-agnostic. Buttons by *position* (south/east/west/north), d-pad, shoulders, triggers (analog 0..1), sticks (-1..1), system buttons, optional gyro/accel, battery. Feedback struct going the other way: rumble strength, player LEDs, light colour.
- **`InputDriver`**: produces `GamepadState` and accepts feedback. v1 has one: `input_bluepad32`, which covers every v1 controller Bluepad32 supports. New controllers land upstream in Bluepad32 where possible, or as a quirks table here.
- **`Target`**: one emulated console-side device. Lifecycle: `begin()`, `startSync()`, `connect()`, `send(const GamepadState&)`, feedback callback, `forget()`. Owns its own L2CAP PSMs and console recognition.
- **`Mapper`**: data-driven table from `GamepadState` to a target's layout (by position by default, by label optionally). Remaps are settings, not code.
- **`Settings`**: NVS-backed key/value store with a schema: selected target, mapping options, paired devices. Written by the web flasher (Web Serial), by controller button combos, or by shell buttons on dedicated hardware.

## No external buttons

The target console is a setting. Holding a combo on the controller at boot cycles targets with LED/rumble feedback. Sync and forget are controller combos too (today: Select+Start 3 s to sync). The BOOT button still works on dev boards but is never required.

## Adding a platform

One `components/target_<x>/`, one `docs/platforms/<x>.md`, one `docs/hw-tests/<x>.md`, host tests, and `support.yaml` rows.

## Known constraints

- Original ESP32 only (S3/C3/C6/H2 lack BT Classic, which Wii, Wii U and Switch need).
- That chip has no USB device port; wired-controller mode needs a second chip on dedicated hardware.
- BTstack (via Bluepad32) is free for non-commercial use; sold hardware needs a BlueKitchen licence.
