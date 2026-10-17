// YUCCA web page: one screen at a time, from picking a console to pairing.

import { boardArt, consoleArt, controllerArt, laptopArt } from "./art.js";
import { CONSOLES, CONTROLLERS, SETTINGS } from "./guide.js";
import * as serial from "./serial.js";
import { initMonitor, quietFor } from "./monitor.js";

const $ = (id) => document.getElementById(id);

// --- Data ---------------------------------------------------------------------

// Used until support.json loads (or if it doesn't).
let support = {
  consoles: [
    { id: "wii", name: "Wii", milestone: "v1" },
    { id: "wiiu", name: "Wii U", milestone: "v1" },
    { id: "switch", name: "Switch", milestone: "v1" },
    { id: "switch2", name: "Switch 2", milestone: "v1" },
  ],
  controllers: Object.keys(CONTROLLERS).map((id) => ({ id, name: CONTROLLERS[id].short })),
  cells: [],
};
let supportLoaded = false;
let manifest = null;

const pick = { console: null, controller: null };
try {
  Object.assign(pick, JSON.parse(localStorage.getItem("yucca-pick") || "{}"));
} catch (e) {
  // Private window or blocked storage: start with nothing picked.
}
function savePick() {
  try {
    localStorage.setItem("yucca-pick", JSON.stringify(pick));
  } catch (e) {
    // Not remembered; the page still works.
  }
}

const v1Consoles = () => support.consoles.filter((c) => c.milestone === "v1");
const nameOf = (list, id) => (list.find((x) => x.id === id) || {}).name || id;
const consoleName = (id) => nameOf(support.consoles, id);
const controllerName = (id) => nameOf(support.controllers, id);

function statusOf(controller, consoleId) {
  const cell = support.cells.find((c) => c.controller === controller && c.console === consoleId);
  if (cell) return cell.status;
  const con = support.consoles.find((c) => c.id === consoleId);
  return con && con.milestone === "v1" ? "planned" : "post-v1";
}

const STATUS = {
  verified: { word: "Confirmed", long: "Confirmed on a real console", tone: "ok" },
  "works-in-ci": { word: "Ready to try", long: "Built and passes automated tests, not yet confirmed on a real console", tone: "try" },
  "in-progress": { word: "Being built", long: "Being built", tone: "wait" },
  planned: { word: "Not yet", long: "Planned, not built yet", tone: "no" },
  blocked: { word: "Not possible yet", long: "Blocked", tone: "no" },
  "post-v1": { word: "Later", long: "Comes after version 1", tone: "no" },
};
const usable = (s) => s === "verified" || s === "works-in-ci";

// --- Screens ------------------------------------------------------------------

const SCREENS = ["welcome", "console", "controller", "install", "connect", "pair-controller", "pair-console", "done"];
const STEPS = [
  { label: "Choose", screens: ["console", "controller"] },
  { label: "Install", screens: ["install"] },
  { label: "Connect", screens: ["connect"] },
  { label: "Pair", screens: ["pair-controller", "pair-console", "done"] },
];
let current = "welcome";

function go(screen, { push = true } = {}) {
  if (!SCREENS.includes(screen)) screen = "welcome";
  const from = current;
  current = screen;
  for (const el of document.querySelectorAll(".view")) {
    const on = el.dataset.screen === screen;
    el.hidden = !on;
    el.classList.toggle("enter-back", on && SCREENS.indexOf(screen) < SCREENS.indexOf(from));
  }
  if (push && location.hash !== `#${screen}`) history.pushState(null, "", `#${screen}`);
  $("stage").scrollTop = 0;
  render();
  const heading = document.querySelector(`.view[data-screen="${screen}"] h1, .view[data-screen="${screen}"] h2`);
  if (heading && from !== screen) {
    heading.setAttribute("tabindex", "-1");
    heading.focus({ preventScroll: true });
  }
  if (screen === "connect" && !serial.isOpen()) autoConnect();
}
window.addEventListener("popstate", () => go(location.hash.slice(1), { push: false }));
document.addEventListener("click", (e) => {
  const t = e.target.closest("[data-go]");
  if (t) go(t.dataset.go);
});

function renderProgress() {
  const at = STEPS.findIndex((s) => s.screens.includes(current));
  $("progress").innerHTML = STEPS.map((s, i) => {
    const state = at < 0 ? "todo" : i < at ? "done" : i === at ? "now" : "todo";
    return `<li class="${state}"><button type="button" data-go="${s.screens[0]}"${
      state === "now" ? ' aria-current="step"' : ""
    }><span class="pip" aria-hidden="true"></span>${s.label}</button></li>`;
  }).join("");
  document.querySelector(".top").classList.toggle("on-welcome", current === "welcome");
}

// What Continue does and says on each screen.
function renderNav() {
  const nav = $("nav");
  nav.hidden = current === "welcome";
  const next = $("next");
  const i = SCREENS.indexOf(current);
  let label = "Continue";
  let enabled = true;
  let strong = true;
  if (current === "console") enabled = !!pick.console;
  if (current === "controller") enabled = !!pick.controller;
  if (current === "install" && flashState !== "done") {
    label = "Skip, it's already installed";
    strong = false;
  }
  if (current === "connect" && !lastInfo) {
    label = "Skip";
    strong = false;
  }
  if (current === "pair-controller" && lastInfo && !lastInfo.controller) strong = false;
  if (current === "pair-console" && lastInfo && !lastInfo.console) strong = false;
  if (current === "done") label = "Back to the start";
  next.textContent = label;
  next.disabled = !enabled;
  next.classList.toggle("primary", strong);
  next.onclick = () => go(current === "done" ? "welcome" : SCREENS[i + 1]);
  $("back").onclick = () => go(SCREENS[Math.max(0, i - 1)]);
}

// --- Pictures -------------------------------------------------------------------

function node(art, label, state = "", on = false) {
  return `<div class="node${on ? " on" : ""}">${art}<span class="node-label">${label}</span>${
    state ? `<span class="node-state">${state}</span>` : ""
  }</div>`;
}
const link = (on) => `<span class="link${on ? " on" : ""}" aria-hidden="true"></span>`;

function chain(info) {
  const ctrl = pick.controller || "dualsense";
  const con = info ? info.target : pick.console || "switch";
  if (!info) {
    return (
      node(controllerArt(ctrl), pick.controller ? controllerName(ctrl) : "Your controller") +
      link(false) +
      node(boardArt(), "YUCCA board") +
      link(false) +
      node(consoleArt(con), pick.console ? consoleName(con) : "Your console")
    );
  }
  return (
    node(controllerArt(ctrl), "Controller", info.controller ? "Connected" : "Not connected", info.controller) +
    link(info.controller) +
    node(boardArt(), "YUCCA", `Version ${String(info.fw).replace(/^v/, "")}`, true) +
    link(info.console) +
    node(consoleArt(con), consoleName(con), info.console ? "Connected" : info.paired ? "Paired" : "Not paired yet", info.console)
  );
}

function tile(kind, id, name, art, note, disabled) {
  const on = pick[kind] === id;
  return `<button type="button" role="radio" aria-checked="${on}" class="tile${on ? " on" : ""}"${
    disabled ? ' aria-disabled="true"' : ""
  } data-kind="${kind}" data-id="${id}">${art}<span class="tile-name">${name}</span>${
    note ? `<span class="tile-note">${note}</span>` : ""
  }</button>`;
}

function renderPickers() {
  $("console-picker").innerHTML = v1Consoles()
    .map((c) => tile("console", c.id, c.name, consoleArt(c.id)))
    .join("");
  $("controller-picker").innerHTML = support.controllers
    .map((c) => {
      const s = pick.console ? statusOf(c.id, pick.console) : null;
      const ok = pick.console ? usable(s) : v1Consoles().some((con) => usable(statusOf(c.id, con.id)));
      const note = ok ? "" : pick.console ? `Not on ${consoleName(pick.console)} yet` : "Not yet";
      return tile("controller", c.id, c.name, controllerArt(c.id), note, !ok);
    })
    .join("");
  $("controller-sub").textContent = pick.console
    ? `These work with your ${consoleName(pick.console)}. One at a time.`
    : "One at a time. You can switch later.";
}

for (const id of ["console-picker", "controller-picker"]) {
  $(id).addEventListener("click", (e) => {
    const t = e.target.closest(".tile");
    if (!t || t.getAttribute("aria-disabled") === "true") return;
    pick[t.dataset.kind] = t.dataset.id;
    savePick();
    render();
    // Picking is the answer: move on once the choice has shown.
    setTimeout(() => go(t.dataset.kind === "console" ? "controller" : "install"), 260);
  });
  // Arrow keys move between tiles, like a radio group.
  $(id).addEventListener("keydown", (e) => {
    const keys = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 };
    if (!(e.key in keys)) return;
    const tiles = [...$(id).querySelectorAll(".tile")];
    const i = tiles.indexOf(document.activeElement);
    if (i < 0) return;
    e.preventDefault();
    tiles[(i + keys[e.key] + tiles.length) % tiles.length].focus();
  });
}

function renderPair() {
  const ctrl = pick.controller || "switch_pro";
  const con = (lastInfo && lastInfo.target) || pick.console || "switch";
  const c = CONTROLLERS[ctrl];
  const k = CONSOLES[con];
  $("pc-art").innerHTML = controllerArt(ctrl, c.hot);
  $("pc-title").textContent = pick.controller ? `Pair your ${c.short}` : "Pair your controller";
  $("pc-steps").innerHTML = (pick.controller
    ? c.pair
    : ["Most controllers have a small sync or pair button. Hold it until a light flashes."]
  )
    .map((s) => `<li>${s}</li>`)
    .join("");

  $("pk-art").innerHTML = consoleArt(con, true);
  $("pk-title").textContent = `Now pair your ${consoleName(con)}`;
  $("pk-lead").textContent = `YUCCA is ready for it for about ${k.seconds} seconds after your controller connects.`;
  $("pk-steps").innerHTML = k.pair.map((s) => `<li>${s}</li>`).join("");

  for (const [id, info, ok, wait, yes] of [
    ["pc-live", lastInfo, lastInfo && lastInfo.controller, `Waiting for your ${c.short}…`, `Your ${c.short} is connected.`],
    [
      "pk-live",
      lastInfo,
      lastInfo && lastInfo.console,
      lastInfo && lastInfo.paired ? "Paired before. Turn on the console and YUCCA connects." : `Waiting for your ${consoleName(con)}…`,
      `Connected to your ${consoleName(con)}.`,
    ],
  ]) {
    const el = $(id);
    el.hidden = !info;
    if (!info) continue;
    el.textContent = ok ? yes : wait;
    el.classList.toggle("ok", !!ok);
  }

  const b = c;
  const keys = (...names) => names.map((n) => `<kbd>${n}</kbd>`).join(" and ");
  $("shortcuts").innerHTML = `
    <div><dt>Pair with a console again</dt><dd>Hold ${keys(b.select, b.start)} for 3 seconds.</dd></div>
    <div><dt>Switch console without a computer</dt><dd>Hold ${keys(b.select, b.home)} for 3 seconds, press left or right until the player lights show your console (1 Wii, 2 Wii U, 3 Switch, 4 Switch 2), then press A.</dd></div>
    <div><dt>Start over</dt><dd>Hold the <kbd>BOOT</kbd> button on the board for 5 seconds.</dd></div>`;
}

function render() {
  renderProgress();
  renderNav();
  $("welcome-chain").innerHTML = chain(null);
  $("done-chain").innerHTML = chain(lastInfo || { fw: "", target: pick.console || "switch", controller: true, console: true, paired: true });
  if (current === "console" || current === "controller") renderPickers();
  if (current.startsWith("pair")) renderPair();
}

// --- Installing -----------------------------------------------------------------

let flashState = "idle"; // idle | running | done | error
$("install-art").innerHTML = laptopArt();
$("boot-art").innerHTML = boardArt("boot");
$("connect-art").innerHTML = boardArt("usb");

if (!serial.supported) {
  $("unsupported").hidden = false;
  $("install").disabled = true;
  $("connect").disabled = true;
}

fetch("firmware/manifest.json")
  .then((r) => (r.ok ? r.json() : Promise.reject()))
  .then((m) => {
    manifest = m;
    $("version-line").textContent = `Installs YUCCA ${m.version}, the latest version.`;
  })
  .catch(() => {
    $("version-line").textContent = "No version has been published yet, so there's nothing to install.";
    $("install").disabled = true;
  });

const FLASH_STEPS = {
  connecting: "Connecting to the board…",
  checking: "Checking the board…",
  downloading: "Downloading YUCCA…",
  erasing: "Clearing the board…",
  writing: "Installing YUCCA…",
  restarting: "Restarting the board…",
};
const FLASH_ERRORS = {
  connect: "Couldn't talk to the board. Unplug it, plug it back in and try again. If that doesn't help, see \"It can't connect to the board\" below.",
  chip: "This board is an {chip}. YUCCA needs an original ESP32, because the newer chips can't talk to a Wii or Switch.",
  download: "Couldn't download YUCCA. Check your internet connection and try again.",
  write: "Installing stopped partway. Keep the board plugged in and try again.",
};

function showFlash(state) {
  flashState = state;
  for (const s of ["idle", "run", "done", "error"]) $(`flash-${s}`).hidden = s !== (state === "running" ? "run" : state);
  renderNav();
}

async function install() {
  let port;
  try {
    port = await serial.release();
    if (!port) port = await serial.choosePort(true);
  } catch (e) {
    return; // Picker closed: nothing to do.
  }
  serial.remember(port);
  showFlash("running");
  const bar = $("flash-bar");
  const onStep = ({ step, percent }) => {
    $("flash-step").textContent = step === "writing" ? `${FLASH_STEPS.writing} ${percent}%` : FLASH_STEPS[step];
    const p = step === "writing" ? percent : step === "restarting" ? 100 : 0;
    bar.style.width = `${p}%`;
    bar.parentElement.setAttribute("aria-valuenow", p);
    bar.parentElement.classList.toggle("busy", step !== "writing" && step !== "restarting");
  };
  try {
    const { flash } = await import("./flash.js");
    await flash(port, manifest, { erase: !$("keep-settings").checked, onStep });
  } catch (e) {
    const text = (FLASH_ERRORS[e.kind] || FLASH_ERRORS.connect).replace("{chip}", e.message);
    $("flash-error-text").textContent = text;
    showFlash("error");
    return;
  }
  showFlash("done");
  // Same board, already allowed: connect without asking, then move on.
  setTimeout(() => go("connect"), 900);
}
$("install").addEventListener("click", install);
$("install-retry").addEventListener("click", () => {
  showFlash("idle");
  install();
});

// --- Connecting and settings ------------------------------------------------------

let lastInfo = null;
let lastSettings = "";
let busy = false;
let poller = null;
let misses = 0;
let applied = false; // the console picked on screen 1 was written to the board

const setStatus = (text) => {
  $("connect-status").textContent = text;
  $("device-status").textContent = lastInfo ? text : "";
};

async function autoConnect() {
  if (!serial.supported) return;
  try {
    await serial.open(false); // only a board this site may already use
  } catch (e) {
    return;
  }
  if (serial.isOpen()) await afterOpen();
}

async function connect() {
  try {
    await serial.open(true);
  } catch (e) {
    setStatus(
      e.name === "NotFoundError"
        ? "No board picked. Click Connect and choose your board in the list."
        : "Couldn't open the board. If another tab or the serial monitor of another app is using it, close that and try again."
    );
    return;
  }
  await afterOpen();
}
$("connect").addEventListener("click", connect);

async function afterOpen() {
  setStatus("Connected. Asking YUCCA how it's doing…");
  if (await refresh(6)) startPolling();
}

async function refresh(tries = 1) {
  for (let i = 0; i < tries; i++) {
    try {
      quietFor(1500);
      const info = await serial.command("yucca_info", serial.infoLine);
      renderDevice(info);
      return true;
    } catch (e) {
      // The board may still be booting.
    }
  }
  setStatus("YUCCA isn't answering. Is it installed on this board? Press the EN button on the board and connect again.");
  return false;
}

function startPolling() {
  clearInterval(poller);
  poller = setInterval(async () => {
    if (busy || !serial.isOpen()) return;
    busy = true;
    try {
      quietFor(1500);
      renderDevice(await serial.command("yucca_info", serial.infoLine, 2500));
      misses = 0;
    } catch (e) {
      if (++misses === 3) setStatus("YUCCA stopped answering. Unplug the board, plug it back in, and connect again.");
    }
    busy = false;
  }, 2500);
}

serial.onState((open) => {
  document.body.classList.toggle("board-on", open);
  if (!open) {
    clearInterval(poller);
    lastInfo = null;
    lastSettings = "";
    applied = false;
    $("device").hidden = true;
    $("connect-idle").hidden = false;
    $("connect-title").textContent = "Connect to your adapter";
    render();
  }
});

function renderDevice(info) {
  const before = lastInfo;
  lastInfo = info;
  $("connect-idle").hidden = true;
  $("device").hidden = false;
  $("connect-title").textContent = "Your adapter";
  $("connect-sub").textContent = "This updates by itself while the board is plugged in.";
  $("live-chain").innerHTML = chain(info);

  const spec = info.settings.target;
  // Use the console picked on the first screen, once per connection.
  if (!applied && pick.console && pick.console !== info.target && spec.choices.some((c) => c.value === pick.console)) {
    applied = true;
    save("target", pick.console, spec, `YUCCA is now set up for your ${consoleName(pick.console)}.`);
  } else if (!applied) {
    applied = true;
    if (!pick.console) {
      pick.console = info.target;
      savePick();
    }
  }

  const settingsJson = JSON.stringify(info.settings);
  if (settingsJson !== lastSettings) {
    lastSettings = settingsJson;
    renderSettings(info);
  }

  // Move on by itself when what this screen waits for happens.
  if (before && current === "pair-controller" && !before.controller && info.controller) setTimeout(() => go("pair-console"), 1200);
  if (before && current === "pair-console" && !before.console && info.console) setTimeout(() => go("done"), 1200);
  render();
}

// Settings named after a console ("wii_mirror", "wiiu_mode") only show for it.
function appliesTo(key, spec, target) {
  if (Array.isArray(spec.targets)) return spec.targets.includes(target);
  const owner = support.consoles.find((c) => key.startsWith(c.id + "_"));
  return !owner || owner.id === target;
}

function renderSettings(info) {
  const spec = info.settings.target;
  const seg = $("console-setting");
  seg.innerHTML = spec.choices
    .map(
      (c) =>
        `<button type="button" role="radio" aria-checked="${c.value === spec.value}" class="seg${
          c.value === spec.value ? " on" : ""
        }" data-value="${c.value}">${c.label}</button>`
    )
    .join("");
  seg.onclick = (e) => {
    const b = e.target.closest(".seg");
    if (b && b.dataset.value !== spec.value) {
      pick.console = b.dataset.value;
      savePick();
      save("target", b.dataset.value, spec);
    }
  };
  const options = $("options");
  options.replaceChildren();
  for (const [key, s] of Object.entries(info.settings)) {
    if (key === "target" || !appliesTo(key, s, info.target)) continue;
    options.append(field(key, s));
  }
}

function field(key, spec) {
  const text = SETTINGS[key] || { label: spec.label };
  const wrap = document.createElement("div");
  wrap.className = "field";
  const id = `set-${key}`;
  const head = document.createElement("div");
  const label = document.createElement(spec.type === "choice" ? "span" : "label");
  label.className = "field-label";
  label.textContent = text.label;
  if (spec.type !== "choice") label.htmlFor = id;
  else label.id = `${id}-label`;
  head.append(label);
  if (text.help) {
    const help = document.createElement("p");
    help.className = "field-help";
    help.textContent = text.help;
    head.append(help);
  }

  if (spec.type === "int" && spec.min === 0 && spec.max === 1) {
    wrap.classList.add("toggle-row");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.id = id;
    input.className = "toggle";
    input.setAttribute("role", "switch");
    input.checked = spec.value === "1";
    input.addEventListener("change", () => save(key, input.checked ? "1" : "0", spec));
    wrap.append(head, input);
    return wrap;
  }
  if (spec.type === "choice") {
    const list = document.createElement("div");
    list.className = "choices";
    list.setAttribute("role", "radiogroup");
    list.setAttribute("aria-labelledby", `${id}-label`);
    for (const c of spec.choices) {
      const row = document.createElement("label");
      row.className = "choice";
      const input = document.createElement("input");
      input.type = "radio";
      input.name = id;
      input.value = c.value;
      input.checked = c.value === spec.value;
      input.addEventListener("change", () => save(key, c.value, spec));
      row.append(input, c.label);
      list.append(row);
    }
    wrap.append(head, list);
    return wrap;
  }
  const select = document.createElement("select");
  select.id = id;
  for (let v = spec.min; v <= spec.max; v++) select.append(new Option(String(v), String(v), false, String(v) === spec.value));
  select.addEventListener("change", () => save(key, select.value, spec));
  wrap.append(head, select);
  return wrap;
}

async function save(key, value, spec, doneText = "Saved.") {
  busy = true;
  setStatus(key === "target" ? "Changing the console…" : `Saving ${(SETTINGS[key] || spec).label}…`);
  try {
    const res = await serial.command(`yucca_set ${key} ${value}`, serial.okLine);
    if (!res.ok) {
      setStatus(`That didn't save: ${res.text}.`);
      busy = false;
      return;
    }
    if (spec.restart) {
      setStatus("Saved. YUCCA is restarting, one moment…");
      await new Promise((r) => setTimeout(r, 2500));
    }
    busy = false;
    if (await refresh(spec.restart ? 10 : 1)) setStatus(doneText);
  } catch (e) {
    busy = false;
    setStatus("YUCCA didn't answer. Check that the board is still plugged in.");
  }
}

$("sync").addEventListener("click", async () => {
  await serial.command("yucca_sync", serial.okLine).catch(() => {});
  go("pair-console");
});
$("forget").addEventListener("click", async () => {
  if (!confirm("Forget your console and every controller? You'll pair both again.")) return;
  await serial.command("yucca_forget", serial.okLine).catch(() => {});
  await refresh();
});

// --- What works --------------------------------------------------------------------

let worksConsole = null;
function renderWorks() {
  const anyVerified = support.cells.some((c) => c.status === "verified");
  $("works-intro").innerHTML = !supportLoaded
    ? "The list couldn't load. Try reloading the page."
    : anyVerified
    ? `"Confirmed" means someone tested it on a real console.`
    : `"Ready to try" means it's built and passes YUCCA's automated tests, but nobody has confirmed it on a real console yet. If you try it, please <a href="https://github.com/MaxLiebe/yucca/issues/new/choose">tell us how it went</a>.`;
  worksConsole = worksConsole || pick.console || v1Consoles()[0].id;
  $("works-tabs").innerHTML = v1Consoles()
    .map(
      (c) =>
        `<button type="button" role="tab" aria-selected="${c.id === worksConsole}" class="seg${
          c.id === worksConsole ? " on" : ""
        }" data-id="${c.id}">${c.name}</button>`
    )
    .join("");
  $("works-list").innerHTML = support.controllers
    .map((c) => {
      const s = STATUS[statusOf(c.id, worksConsole)];
      return `<li${pick.controller === c.id ? ' class="mine"' : ""}><span>${c.name}</span><span class="status tone-${s.tone}" title="${s.long}">${s.word}</span></li>`;
    })
    .join("");
}
$("works-tabs").addEventListener("click", (e) => {
  const b = e.target.closest(".seg");
  if (b) {
    worksConsole = b.dataset.id;
    renderWorks();
  }
});
$("works-open").addEventListener("click", () => {
  renderWorks();
  $("works").showModal();
});
$("works-close").addEventListener("click", () => $("works").close());
$("works").addEventListener("click", (e) => {
  if (e.target === $("works")) $("works").close(); // a click on the backdrop
});

fetch("support.json")
  .then((r) => (r.ok ? r.json() : Promise.reject()))
  .then((data) => {
    support = data;
    supportLoaded = true;
    render();
  })
  .catch(() => {});

// --- Start ---------------------------------------------------------------------------

initMonitor();
go(location.hash.slice(1) || "welcome", { push: false });
