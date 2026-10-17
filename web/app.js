// YUCCA web page: the setup guide, settings over Web Serial, support matrix.

import { boardArt, consoleArt, controllerArt, laptopArt } from "./art.js";
import { CONSOLES, CONTROLLERS, SETTINGS } from "./guide.js";

const $ = (id) => document.getElementById(id);
const encoder = new TextEncoder();
const hasSerial = "serial" in navigator;

// The board's last yucca_info answer, while connected.
let lastInfo = null;
let lastSettings = "";

// --- What the visitor picked -------------------------------------------------

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

const pick = { console: null, controller: null };
try {
  Object.assign(pick, JSON.parse(localStorage.getItem("yucca-pick") || "{}"));
} catch (e) {
  // Private window or blocked storage: start with nothing picked.
}

function choose(kind, id) {
  pick[kind] = id;
  try {
    localStorage.setItem("yucca-pick", JSON.stringify(pick));
  } catch (e) {
    // Not remembered; the page still works.
  }
  renderGuide();
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
const usable = (status) => status === "verified" || status === "works-in-ci";

// --- Pictures that follow the picks ------------------------------------------

function node(art, label, state = "", on = false) {
  return `<div class="node${on ? " on" : ""}">${art}<span class="node-label">${label}</span>${
    state ? `<span class="node-state">${state}</span>` : ""
  }</div>`;
}
const link = (on) => `<span class="link${on ? " on" : ""}" aria-hidden="true"></span>`;

function renderHero() {
  $("hero-chain").innerHTML =
    node(controllerArt(pick.controller || "dualsense"), pick.controller ? controllerName(pick.controller) : "Your controller") +
    link(false) +
    node(boardArt(), "YUCCA board") +
    link(false) +
    node(consoleArt(pick.console || "switch"), pick.console ? consoleName(pick.console) : "Your console");
}

function tile(kind, id, name, art, note) {
  const on = pick[kind] === id;
  return `<button type="button" role="radio" aria-checked="${on}" class="tile${on ? " on" : ""}" data-kind="${kind}" data-id="${id}">
    ${art}<span class="tile-name">${name}</span>${note ? `<span class="tile-note">${note}</span>` : ""}</button>`;
}

function renderPickers() {
  $("console-picker").innerHTML = v1Consoles()
    .map((c) => tile("console", c.id, c.name, consoleArt(c.id)))
    .join("");
  $("controller-picker").innerHTML = support.controllers
    .map((c) => {
      const s = pick.console ? statusOf(c.id, pick.console) : null;
      const anyUsable = v1Consoles().some((con) => usable(statusOf(c.id, con.id)));
      const note = pick.console ? (usable(s) ? "" : STATUS[s].word) : anyUsable ? "" : "Not yet";
      return tile("controller", c.id, c.name, controllerArt(c.id), note);
    })
    .join("");

  const combo = $("combo");
  if (pick.console && pick.controller) {
    const s = statusOf(pick.controller, pick.console);
    combo.className = `combo is-${STATUS[s].tone}`;
    combo.textContent = usable(s)
      ? `${controllerName(pick.controller)} on ${consoleName(pick.console)}: ${STATUS[s].word.toLowerCase()}. Your steps are below.`
      : `${controllerName(pick.controller)} on ${consoleName(pick.console)} isn't supported yet.`;
  } else {
    combo.className = "combo";
    combo.textContent = "";
  }
}

for (const id of ["console-picker", "controller-picker"]) {
  $(id).addEventListener("click", (e) => {
    const t = e.target.closest(".tile");
    if (t) choose(t.dataset.kind, t.dataset.id);
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

function buttonNames(id) {
  const c = CONTROLLERS[id];
  if (!c) return { select: "Select", home: "Home", start: "Start" };
  return { select: c.select, home: c.home, start: c.start };
}

function renderPair() {
  const ctrl = pick.controller || "switch_pro";
  const con = pick.console || "switch";
  const c = CONTROLLERS[ctrl];
  const k = CONSOLES[con];

  $("pair-controller-art").innerHTML = controllerArt(ctrl, c.hot);
  $("pair-controller-title").textContent = pick.controller ? `First, your ${c.short}` : "First, your controller";
  $("pair-controller-steps").innerHTML = (pick.controller
    ? c.pair
    : ["Pick your controller above to see exactly what to press. Most controllers have a small sync or pair button you hold until a light flashes."]
  )
    .map((s) => `<li>${s}</li>`)
    .join("");

  $("pair-console-art").innerHTML = consoleArt(con, true);
  $("pair-console-title").textContent = pick.console ? `Then, your ${consoleName(con)}` : "Then, your console";
  $("pair-console-lead").textContent = `Once the controller is connected, YUCCA opens its pairing window by itself for about ${k.seconds} seconds.`;
  $("pair-console-steps").innerHTML = (pick.console ? k.pair : ["Pick your console above to see where its pairing button is."])
    .map((s) => `<li>${s}</li>`)
    .join("");

  const b = buttonNames(ctrl);
  const keys = (...names) => names.map((n) => `<kbd>${n}</kbd>`).join(" and ");
  $("shortcuts").innerHTML = `
    <div><dt>Missed the pairing window, or want another console?</dt>
      <dd>Hold ${keys(b.select, b.start)} for 3 seconds.</dd></div>
    <div><dt>Switch console without a computer</dt>
      <dd>Hold ${keys(b.select, b.home)} for 3 seconds. Press left or right until the player lights show your console (1 Wii, 2 Wii U, 3 Switch, 4 Switch 2), then press A.</dd></div>
    <div><dt>Start over</dt>
      <dd>Hold the <kbd>BOOT</kbd> button on the board for 5 seconds. YUCCA forgets your console and controllers.</dd></div>`;
}

function renderWorks() {
  if (!supportLoaded) return;
  const anyVerified = support.cells.some((c) => c.status === "verified");
  $("works-intro").innerHTML = anyVerified
    ? `Combinations marked "Confirmed" were tested by someone on a real console.`
    : `Everything marked "Ready to try" is built and passes YUCCA's automated tests, but nobody has confirmed it on a real console yet. If you try it, please <a href="https://github.com/MaxLiebe/yucca/issues/new/choose">tell us how it went</a>.`;

  const consoles = pick.console ? [pick.console] : v1Consoles().map((c) => c.id);
  $("works-list").innerHTML = consoles
    .map(
      (con) => `<div class="works-console">
        <h3>${consoleName(con)}</h3>
        <ul class="works">${support.controllers
          .map((c) => {
            const s = STATUS[statusOf(c.id, con)];
            return `<li${pick.controller === c.id ? ' class="mine"' : ""}><span>${c.name}</span><span class="status tone-${s.tone}" title="${s.long}">${s.word}</span></li>`;
          })
          .join("")}</ul></div>`
    )
    .join("");

  const table = document.createElement("table");
  const head = table.createTHead().insertRow();
  head.append(Object.assign(document.createElement("th"), { textContent: "Controller" }));
  for (const con of support.consoles) head.append(Object.assign(document.createElement("th"), { textContent: con.name }));
  const body = table.createTBody();
  for (const ctrl of support.controllers) {
    const row = body.insertRow();
    row.insertCell().textContent = ctrl.name;
    for (const con of support.consoles) {
      const s = STATUS[statusOf(ctrl.id, con.id)];
      const td = row.insertCell();
      td.innerHTML = `<span class="status tone-${s.tone}" title="${s.long}">${s.word}</span>`;
    }
  }
  const wrap = document.createElement("div");
  wrap.className = "table-wrap";
  wrap.append(table);
  $("matrix").replaceChildren(wrap);
  $("matrix-wrap").hidden = false;
}

function renderGuide() {
  renderHero();
  renderPickers();
  renderPair();
  renderWorks();
  if (lastInfo) renderDevice(lastInfo);
}

$("laptop-art").innerHTML = laptopArt();
$("boot-art").innerHTML = boardArt("boot");

if (!hasSerial) {
  $("unsupported").hidden = false;
  $("connect").disabled = true;
}

fetch("support.json")
  .then((r) => (r.ok ? r.json() : Promise.reject()))
  .then((data) => {
    support = data;
    supportLoaded = true;
    renderGuide();
  })
  .catch(() => {
    $("works-intro").textContent = "The list of what works couldn't load. Try reloading the page.";
  });

renderGuide();

// --- Release ---------------------------------------------------------------

fetch("firmware/manifest.json")
  .then((r) => (r.ok ? r.json() : Promise.reject()))
  .then((m) => ($("version-line").textContent = `Installs YUCCA ${m.version}, the latest version.`))
  .catch(() => ($("version-line").textContent = "No version has been published yet, so there's nothing to install."));

// --- Serial ----------------------------------------------------------------

let port = null;
let reader = null;
let loop = null;
let closing = false;
let buffer = "";
const waiters = [];

async function readLoop(p) {
  const decoder = new TextDecoder();
  // Framing/overrun errors (e.g. while the board restarts) are not fatal:
  // port.readable comes back, so keep reading until the port is closed.
  while (!closing && p.readable) {
    reader = p.readable.getReader();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let nl;
        while ((nl = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, nl).replace(/\r$/, "");
          buffer = buffer.slice(nl + 1);
          for (const w of [...waiters]) {
            try {
              w(line);
            } catch (e) {
              // A garbled line; wait for the next one.
            }
          }
        }
      }
    } catch (e) {
      // Fall through and re-acquire the reader.
    } finally {
      reader.releaseLock();
      reader = null;
    }
  }
}

// Close the port so the installer (or another connect) can open it.
async function disconnect() {
  stopPolling();
  if (!port) return;
  const p = port;
  port = null;
  closing = true;
  try {
    if (reader) await reader.cancel();
    await loop;
    await p.close();
  } catch (e) {
    // Already gone (unplugged).
  }
  closing = false;
  buffer = "";
  lastInfo = null;
  $("device").hidden = true;
  $("connect").hidden = false;
  renderLive(null);
}

// Send a console command; resolve with the first line `match` accepts.
function command(cmd, match, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    const onLine = (line) => {
      const m = match(line);
      if (m !== undefined) {
        done();
        resolve(m);
      }
    };
    const timer = setTimeout(() => {
      done();
      reject(new Error(`No answer to ${cmd}`));
    }, timeoutMs);
    const done = () => {
      clearTimeout(timer);
      waiters.splice(waiters.indexOf(onLine), 1);
    };
    waiters.push(onLine);
    if (!port || !port.writable) {
      done();
      reject(new Error("Not connected"));
      return;
    }
    const writer = port.writable.getWriter();
    writer.write(encoder.encode(cmd + "\r\n")).finally(() => writer.releaseLock());
  });
}

const infoLine = (line) => {
  const i = line.indexOf("YUCCA {");
  return i >= 0 ? JSON.parse(line.slice(i + 6)) : undefined;
};
const okLine = (line) => {
  const m = line.match(/^(?:.*>\s*)?(OK|ERR)\b ?(.*)$/);
  return m ? { ok: m[1] === "OK", text: m[2] } : undefined;
};

async function connect() {
  await disconnect();
  let p;
  try {
    p = await navigator.serial.requestPort();
    await p.open({ baudRate: 115200 });
    // Keep the board running: don't hold EN/BOOT through DTR/RTS.
    await p.setSignals({ dataTerminalReady: false, requestToSend: false });
  } catch (e) {
    if (e.name === "NotFoundError") {
      setStatus("No board picked. Click the button again and choose your board in the list.");
    } else {
      setStatus("Couldn't open the board. If another page or app is using it, close that and try again.");
    }
    return;
  }
  port = p;
  loop = readLoop(p);
  setStatus("Connected. Asking YUCCA how it's doing…");
  if (await refresh(5)) startPolling();
}

async function refresh(tries = 1) {
  for (let i = 0; i < tries; i++) {
    try {
      const info = await command("yucca_info", infoLine);
      renderDevice(info);
      return true;
    } catch (e) {
      // The board may still be booting.
    }
  }
  setStatus("YUCCA isn't answering. Is it installed on this board? Press the EN button on the board and connect again.");
  return false;
}

function setStatus(text) {
  $("status").textContent = text;
}

// Keep the connection pictures live while pairing.
let poller = null;
let busy = false;
let misses = 0;
function startPolling() {
  stopPolling();
  poller = setInterval(async () => {
    if (busy || !port) return;
    busy = true;
    try {
      renderDevice(await command("yucca_info", infoLine, 2500));
      misses = 0;
    } catch (e) {
      if (++misses === 3) setStatus("YUCCA stopped answering. Unplug the board, plug it back in, and connect again.");
    }
    busy = false;
  }, 3000);
}
function stopPolling() {
  clearInterval(poller);
  poller = null;
}

// --- Device panel and settings ------------------------------------------------

function renderLive(info) {
  for (const [id, text] of [
    ["live-controller", info && (info.controller ? "Your controller is connected." : "Waiting for your controller…")],
    [
      "live-console",
      info &&
        (info.console
          ? "Connected to your console. You're done."
          : info.paired
          ? "Paired before. Turn on the console and YUCCA connects to it."
          : "Waiting for the console…"),
    ],
  ]) {
    const el = $(id);
    el.hidden = !info;
    if (!info) continue;
    el.textContent = text;
    el.classList.toggle("ok", id === "live-controller" ? info.controller : info.console);
  }
}

function renderDevice(info) {
  lastInfo = info;
  $("connect").hidden = true;
  $("device").hidden = false;
  if (!busy) setStatus("");
  $("fw").textContent = `Version ${String(info.fw).replace(/^v/, "")}`;
  const target = info.target;

  // The visitor's console pick follows the board once it's connected.
  if (!pick.console) pick.console = target;

  $("live-chain").innerHTML =
    node(
      controllerArt(pick.controller || "dualsense"),
      "Controller",
      info.controller ? "Connected" : "Not connected",
      info.controller
    ) +
    link(info.controller) +
    node(boardArt(), "YUCCA", "Ready", true) +
    link(info.console) +
    node(
      consoleArt(target),
      consoleName(target),
      info.console ? "Connected" : info.paired ? "Paired" : "Not paired yet",
      info.console
    );

  const spec = info.settings.target;
  const want = pick.console;
  const canWant = spec && spec.choices.some((c) => c.value === want);
  $("suggest").hidden = !(want && want !== target && canWant);
  if (want && want !== target && canWant) {
    $("suggest-text").textContent = `You picked ${consoleName(want)} above, but YUCCA is set up for ${consoleName(target)}.`;
    $("suggest-apply").textContent = `Use ${consoleName(want)}`;
    $("suggest-apply").onclick = () => save("target", want, spec);
  }

  const settingsJson = JSON.stringify(info.settings);
  if (settingsJson !== lastSettings) {
    lastSettings = settingsJson;
    renderSettings(info);
  }
  renderLive(info);
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
      choose("console", b.dataset.value);
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
  head.className = "field-text";
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
  for (let v = spec.min; v <= spec.max; v++) {
    select.append(new Option(String(v), String(v), false, String(v) === spec.value));
  }
  select.addEventListener("change", () => save(key, select.value, spec));
  wrap.append(head, select);
  return wrap;
}

async function save(key, value, spec) {
  busy = true;
  const name = (SETTINGS[key] || spec).label;
  setStatus(`Saving ${key === "target" ? "your console" : name}…`);
  try {
    const res = await command(`yucca_set ${key} ${value}`, okLine);
    if (!res.ok) {
      setStatus(`That didn't save: ${res.text}.`);
      busy = false;
      return;
    }
    if (spec.restart) {
      setStatus("Saved. YUCCA is restarting, one moment…");
      await new Promise((r) => setTimeout(r, 2500));
      busy = false;
      if (await refresh(10)) setStatus("Saved.");
    } else {
      busy = false;
      await refresh();
      setStatus("Saved.");
    }
  } catch (e) {
    busy = false;
    setStatus("YUCCA didn't answer. Check that the board is still plugged in.");
  }
}

$("connect").addEventListener("click", connect);
if (hasSerial) {
  navigator.serial.addEventListener("disconnect", (e) => {
    if (e.target === port) {
      disconnect();
      setStatus("The board was unplugged. Plug it back in and connect again.");
    }
  });
}
// The installer needs the port: let go of it first.
document.querySelector("esp-web-install-button")?.addEventListener(
  "click",
  () => {
    if (port) {
      disconnect();
      setStatus("Disconnected so the installer can use the board. Connect again after installing.");
    }
  },
  true
);
$("sync").addEventListener("click", async () => {
  await command("yucca_sync", okLine).catch(() => {});
  setStatus(`Pairing window open. Start pairing on your ${consoleName(lastInfo ? lastInfo.target : pick.console)} now.`);
  $("pair").scrollIntoView({ behavior: "smooth" });
});
$("forget").addEventListener("click", async () => {
  if (!confirm("Forget your console and every controller? You'll need to pair both again.")) return;
  await command("yucca_forget", okLine).catch(() => {});
  await refresh();
});
