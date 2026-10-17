# Architecture

```
controller --BT--> InputDriver --GamepadState--> Mapper --> Target --BT--> console
           <------ feedback (rumble, LEDs) <------------------------
```

One original ESP32, one radio, one BTstack instance shared by the input side (Bluepad32) and the target side. The ESP32 stays master on every link (the radio stalls when it is slave on one link and master on another).

## Interfaces (`components/yucca_core`)

Headers live in `components/yucca_core/include/yucca/`. Everything runs on the BTstack run loop, so there is no locking.

- **`GamepadState`** (`gamepad.h`): canonical, controller-agnostic. Buttons by *position* (`South/East/West/North`, shoulders, trigger clicks, stick clicks, `Select/Start/Home/Capture`, d-pad), sticks -1..1 with +y up and the deadzone already applied, analog triggers 0..1, optional gyro/accel, battery %. **`Feedback`** goes the other way: two rumble motors and player LEDs.
- **`InputDriver`** (`input.h`): reports `on_input_ready(InputLink)`, `on_input_state`, `on_input_gone` to the app; takes `set_scanning`, `apply(Feedback)`, `forget_all`. v1 has one: `input_bluepad32`.
- **`Target`** (`target.h`): one emulated console-side device. `begin(listener, settings)`, `start_sync()`, `forget()`, `on_input_ready/gone`, `update(state)` on every controller report, `tick(now_ms)` every 20 ms. Reports `connected`, `disconnected`, `sync_changed` and `feedback` to a `TargetListener`. Owns its console's reconnect policy.
- **`Mapper`** (`mapper.h`): static route table from canonical buttons to a target's bits. `FaceLayout::Position` (default) or `Label` (Xbox-style labels: A presses the console's A).
- **`Settings`** (`settings.h`): typed key/value store; `MemorySettings` for host tests, `NvsSettings` on the device. Shared keys in `yucca::keys`.

The app in `main/` owns one input and one target and wires them: input state goes to `Target::update`, target feedback goes to `InputDriver::apply`, and the app decides when to scan for controllers and handles the sync/forget gestures.

## No external buttons

The target console is a setting. Holding a combo on the controller at boot cycles targets with LED/rumble feedback. Sync and forget are controller combos too (today: Select+Start 3 s to sync). The BOOT button still works on dev boards but is never required.

## Adding a platform

One `components/target_<x>/`, one `docs/platforms/<x>.md`, one `docs/hw-tests/<x>.md`, host tests, and `support.yaml` rows.

## Known constraints

- Original ESP32 only (S3/C3/C6/H2 lack BT Classic, which Wii, Wii U and Switch need).
- That chip has no USB device port; wired-controller mode needs a second chip on dedicated hardware.
- BTstack (via Bluepad32) is free for non-commercial use; sold hardware needs a BlueKitchen licence.
