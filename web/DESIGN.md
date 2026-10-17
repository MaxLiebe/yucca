# Installer design

Read this before changing the page. It follows the [anti-slop](https://github.com/miqdadbadjuber/anti-slop) filter: every choice below has a reason, and nothing on the page is invented.

**Reading this as:** a setup assistant for people who play games and don't build electronics, friendly and calm, in the spirit of Apple's setup screens. Dials: energy 1 (calm), rhythm 2 (one layout family, a few breaks), motion 2 (screens slide in the direction you move; nothing loops except "waiting" indicators while the page really waits).

| Choice | Reason |
| --- | --- |
| One screen at a time, Back and Continue at the bottom, no page scroll | One question or task per screen is easier to follow than a long page. The URL hash follows the screen, so the browser's back button works too. |
| Order: console, controller, install, connect, pair controller, pair console, done | The order the visitor does things in. Picking first lets every later screen talk about their own controller and console. |
| Picking a tile moves on by itself | The pick is the answer; a separate Continue click adds nothing. |
| Install runs in the page (esptool-js), not in a pop-up dialog | It looks like the rest of the page, the progress bar sits where the button was, and the same board is reused to connect afterwards without asking again. |
| Screens move on when the board reports progress | When the controller or console connects, the pairing screens advance by themselves, so the visitor sees it worked. |
| Serial monitor in a side panel, opened from the top bar | One click from anywhere; it reuses the open connection and hides the page's own status questions. |
| Colours from a potted yucca: cream wall `#efe9df`, white pot `#fbf9f5`, oak `#d9c4a3`, trunk `#8a6a4b`, leaf `#2e5e3e` | The project's name and a warm, friendly mood. Leaf green is the one accent: primary buttons, highlighted buttons in drawings, live connections. Trunk brown is for small labels and numbers. |
| Light mode only | The warm cream palette is the identity; a dark version would lose it. |
| Rounded type: SF Pro Rounded on Apple devices, Nunito elsewhere | Rounded letters match the friendly tone and the soft shapes of the drawings. |
| Line illustrations in `art.js` | They show the exact button or menu to use. Original drawings, no brand artwork or logos. |
| The chain (controller, board, console joined by a line) | The one motif: it explains the idea on the first screen, shows live status on the connect screen, and the step indicator at the top reuses its dashed and solid line. |
| Pill buttons, 24px cards, 14px small controls | Three radii, used everywhere. |
| Status words ("Ready to try", "Confirmed") | Plain words. "Confirmed" only appears once a hardware report sets `verified` in `support.yaml`. |

Copy: plain words ("the board", "your console"), no em dashes, no numbers or claims the repo can't back up, every button does what its label says.

Pairing steps per controller and console live in `guide.js`. They come from `docs/controllers.md` and `docs/hw-tests/`; change them together.

Files: `app.js` screens and settings, `serial.js` the one serial connection, `flash.js` installing, `monitor.js` the serial monitor, `art.js` drawings, `guide.js` pairing text.

To preview locally: copy `web/` into a folder, add `support.json` (`python3 tools/support_matrix.py --json site/support.json`) and a `firmware/` folder with `manifest.json` and the `.bin` from a release, then serve it on localhost.
