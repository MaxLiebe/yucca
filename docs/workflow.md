# Workflow

How work moves from idea to merged code. Written for humans and agents.

## Roles

| Role | Does |
| --- | --- |
| Maintainer | Sets priorities, runs hardware test scripts on real consoles, merges. Only humans set `verified` in `support.yaml`. |
| Implementer (human or agent) | Takes one issue, opens one PR, adds host tests, drives CI green. |
| Researcher | Takes a spike issue. Delivers `docs/platforms/<x>.md` with a go/no-go and a test plan. No code. |
| Reviewer | A fresh pair of eyes (or a fresh agent session) on the diff, against the issue's acceptance criteria and the platform doc. |
| Local worker | Optional local model for bounded, test-checked chores (see AGENTS.md). |

## Loop

1. **Issue** from a template: work item, spike, or hardware test report.
2. **Branch + draft PR**, linked to the issue.
3. **Host tests + CI green**: firmware build, host tests, `support.yaml` check.
4. **Review** against acceptance criteria. Fix in the same PR.
5. **Hardware test** if the PR changes anything a console sees: the PR names the `docs/hw-tests/` script, a maintainer runs it and files a report.
6. **Merge** once CI is green and review is clean. `support.yaml` goes to `works-in-ci`; a hardware report takes it to `verified`.

## Size

An issue should fit in one focused PR (roughly under 600 changed lines excluding fixtures and generated files). Bigger work is split into issues that each leave `main` building.

## Phases (v1)

0. Foundation: this repo does what wii-gamepad-adapter does, on the new architecture.
1. Zero-button UX: settings in NVS, target selection and sync from the controller, Web Serial settings.
2. Web flasher: ESP Web Tools install page plus settings page and support matrix.
3. Wii U (Pro Controller emulation).
4. Switch and Switch 2 (Pro Controller emulation).
5. Controller gaps: every v1 controller on every v1 console.

PS4/PS5 come after v1, behind a research spike (controller authentication).
