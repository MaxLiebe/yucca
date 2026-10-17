# YUCCA

**Your Universal Cross-platform Controller Adapter.** Free and open-source ESP32 firmware that lets almost any Bluetooth controller play on most game consoles. The ESP32 bridges the controller and the console; no extra buttons or wiring needed.

Work in progress. Builds on [wii-gamepad-adapter](https://github.com/MaxLiebe/wii-gamepad-adapter) and [WiimoteAnything](https://github.com/MaxLiebe/wiimote-anything).

## What works

Generated from [`support.yaml`](support.yaml). The Wii column already works in [wii-gamepad-adapter](https://github.com/MaxLiebe/wii-gamepad-adapter); it turns green here once ported and retested.

<!-- support-matrix:start -->
| Controller | Wii | Wii U | Switch | Switch 2 | PS4 | PS5 |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| PlayStation 3 controller | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| DualShock 4 | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| DualSense | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Wii Remote + extensions | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Wii U Pro Controller | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Switch Pro Controller | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Switch Joy-Con | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Switch 2 Joy-Con | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| 8BitDo controllers | ⬜ | ⬜ | ⬜ | ⬜ | — | — |
| Stadia controller | ⬜ | ⬜ | ⬜ | ⬜ | — | — |

✅ verified on hardware · 🧪 builds and passes tests, not yet verified · 🚧 in progress · ⬜ planned for v1 · ⛔ blocked · — after v1
<!-- support-matrix:end -->

## Contributing

Start with [AGENTS.md](AGENTS.md) and [docs/workflow.md](docs/workflow.md).

## License

MIT (see `LICENSE`).
