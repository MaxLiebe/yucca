# Installer design

Read this before changing the page. The page follows the [anti-slop](https://github.com/miqdadbadjuber/anti-slop) filter: every choice below has a reason, and nothing on the page is invented.

**Reading this as:** a setup guide for people who play games and don't build electronics, in a calm, Apple-Support-like illustrated style. Dials: energy 1 (calm), rhythm 2 (consistent, with a few breaks), motion 1 (hover and selection only, no loops).

| Choice | Reason |
| --- | --- |
| Page order: pick, install, set up, pair, what works | It is the order the visitor does things in. Picking first lets every later step speak about their own controller and console. |
| System font stack (SF Pro, Segoe UI) | The browser's own install and port dialogs use it, so the page and those dialogs feel like one thing. Nothing to download. |
| Warm off-white, near-black ink, one green accent | Green is the yucca and was already the site's colour. One accent marks what to press next: the primary button, the highlighted button in a drawing, a live connection. |
| Line illustrations, drawn in `art.js` | They show the exact button or menu to use, which a photo of one model can't. Original drawings, no brand artwork or logos. |
| The chain (controller, board, console joined by a line) | The one motif. The hero explains the idea with it; the setup panel reuses it, and the line turns solid when that link is live. Highlight rings are dashed like the chain's line. |
| Large plain numerals for chapters, not numbered circles | The steps are real and in order; the numeral is enough. |
| Radius 20px for panels and tiles, 12px for buttons | Two sizes, used everywhere. |
| Status words ("Ready to try", "Confirmed") instead of icons | Plain words a visitor understands; "Confirmed" only appears once a human hardware report sets `verified` in `support.yaml`. |
| Light and dark follow the system | Like Apple's own pages. Both themes are checked before merging. |

Rules for copy: plain words ("the board", "your console"), no em dashes, no numbers or claims that the repo can't back up, every button does exactly what its label says.

Pairing steps per controller and console live in `guide.js`. They come from `docs/controllers.md` and `docs/hw-tests/`; change them together.

To preview locally: copy `web/` into a folder, add `support.json` (`python3 tools/support_matrix.py --json site/support.json`), and serve it over http(s) on localhost.
