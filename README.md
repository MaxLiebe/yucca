# YUCCA

**Your Universal Cross-platform Controller Adapter.** Free and open-source ESP32 firmware that lets almost any Bluetooth controller play on most game consoles. The ESP32 bridges the controller and the console; no extra buttons or wiring needed.

Work in progress. Builds on [wii-gamepad-adapter](https://github.com/MaxLiebe/wii-gamepad-adapter) and [WiimoteAnything](https://github.com/MaxLiebe/wiimote-anything).

## Use it

You need an **original ESP32** board (ESP32-WROOM-32 / WROVER; the S3, C3, C6 and H2 lack Bluetooth Classic).

**Easiest:** open the [web installer](https://maxliebe.github.io/yucca/) in Chrome or Edge, click Install, then pick your console on the same page.

**From source** (ESP-IDF 5.5 or 6.x):

```bash
git clone --recursive https://github.com/MaxLiebe/yucca.git
cd yucca
idf.py set-target esp32
idf.py build flash monitor
```

1. Pair your controller ([how, per controller](docs/controllers.md)). The log shows `Gamepad ready`.
2. With no console paired yet, the console's sync window opens by itself. On a Wii, press the red **SYNC** button within 20 s.
3. After that, turning the controller on is enough: YUCCA reconnects to it, then to the console.

| Gesture | Effect |
| --- | --- |
| Hold **Select + Start** 3 s | Sync with a console again |
| **BOOT** short press | Same |
| Hold **BOOT** 5 s | Forget the console and all controllers |
| Hold **Select + Home** 3 s | Console menu: the player LEDs show the console (1 Wii, 2 Wii U, 3 Switch, 4 Switch 2), d-pad left/right to change, A to confirm (YUCCA restarts), Select to cancel |

Settings can also be changed over USB serial (115200): `yucca_info` shows everything, `yucca_set <key> <value>` changes it.

## What works

Generated from [`support.yaml`](support.yaml). The Wii column already works in [wii-gamepad-adapter](https://github.com/MaxLiebe/wii-gamepad-adapter); it turns green here once ported and retested.

<!-- support-matrix:start -->
| Controller | Wii | Wii U | Switch | Switch 2 | PS4 | PS5 |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| PlayStation 3 controller | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| DualShock 4 | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| DualSense | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Wii Remote + extensions | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Wii U Pro Controller | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Switch Pro Controller | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Switch Joy-Con | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Switch 2 Joy-Con | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| 8BitDo controllers | 🧪 | ⬜ | ⬜ | ⬜ | — | — |
| Stadia controller | 🧪 | ⬜ | ⬜ | ⬜ | — | — |

✅ verified on hardware · 🧪 builds and passes tests, not yet verified · 🚧 in progress · ⬜ planned for v1 · ⛔ blocked · — after v1
<!-- support-matrix:end -->

## Contributing

Start with [AGENTS.md](AGENTS.md) and [docs/workflow.md](docs/workflow.md).

## License

MIT (see `LICENSE`).
