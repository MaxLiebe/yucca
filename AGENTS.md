# AGENTS.md

Read this first. It is the whole onboarding for humans and agents; do not scan the tree to rediscover it.

## What this is

YUCCA: ESP32 firmware that bridges a Bluetooth controller (input) to a game console (target). One original ESP32 (BT Classic + BLE, single radio). C++17 app code on ESP-IDF; C is fine inside protocol components.

v1 consoles: Wii, Wii U, Switch, Switch 2. PS4/PS5 come after v1.
v1 controllers: DS3, DS4, DualSense, Wiimote + extensions, Wii U Pro, Switch Pro, Joy-Con, Switch 2 Joy-Con, 8BitDo, Stadia.

## Repo map

| Path | What |
| --- | --- |
| `main/` | Boot: load settings, pick input + target, run the loop. |
| `components/wiimote_anything/` | Wii Remote emulation library, `git subtree` of MaxLiebe/wiimote-anything. Push fixes back with `git subtree push`. |
| `external/bluepad32/` | Bluepad32 submodule (gamepads, and the BTstack we build on). Its BTstack patches are applied at configure time. |
| `components/yucca_core/` | `GamepadState`, `Feedback`, `InputDriver`, `Target`, `Mapper`, `Settings` (memory + NVS). No ESP-IDF headers except `nvs_settings`. |
| `components/input_bluepad32/` | Controller side: Bluepad32 -> `GamepadState`, feedback to the controller. `src/convert.*` is host-tested. |
| `components/target_*/` | Console side, one per emulated device (planned). |
| `support.yaml` | Source of truth for what works. README table is generated from it. |
| `tools/support_matrix.py` | Validates `support.yaml`, renders the README table. |
| `docs/architecture.md` | Interfaces and data flow. |
| `docs/workflow.md` | How work moves from issue to merge. |
| `docs/platforms/<x>.md` | Protocol notes per console/controller. Read the one you need, nothing else. |
| `docs/hw-tests/<x>.md` | Manual test script per target, run by a human on real hardware. |

## Commands

```bash
git submodule update --init --recursive
idf.py set-target esp32 && idf.py build      # ESP-IDF 5.5 or 6.x
python3 tools/support_matrix.py --check      # support.yaml valid and README in sync
python3 tools/support_matrix.py --write      # regenerate the README table
```

Host tests (Linux, no hardware) live next to each component under `test/host/` (WiimoteAnything: `tests/`) and run in the CI `host-tests` job. A new component adds its own step there.

## Rules

1. One issue, one PR, one scope. Do not touch files outside the issue's scope; open a new issue instead.
2. Every logic change comes with a host test. Protocol code is tested against captured fixtures where they exist.
3. Protocol facts go in `docs/platforms/<x>.md` with sources, once. Read that doc instead of re-researching.
4. Update `support.yaml` in the same PR. Agents may set `in-progress` or `works-in-ci`. Only a human hardware test report sets `verified`.
5. Merge gate: CI green and review pass clean. Agents merge only when the maintainer has said so for the current work. Console-visible changes stay `works-in-ci` in `support.yaml` until a hardware test report.
6. Keep the default branch building. Small PRs; split anything large.
7. Licensing: project code is MIT. Bluepad32 is Apache-2.0; BTstack (via Bluepad32) needs a commercial licence for sold hardware. Do not add GPL code.

## Delegating to the local model

Maintainers may have a local Qwen worker (MCP `local-worker`). Give it only bounded tasks whose result a test or compiler checks (tables, boilerplate, fixtures, log summaries). Never protocol state machines, crypto, or pairing logic. Inputs over 200K characters are truncated: split them.
