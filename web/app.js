// YUCCA web page: release info, settings over Web Serial, support matrix.

const $ = (id) => document.getElementById(id);
const encoder = new TextEncoder();

if (!("serial" in navigator)) {
  $("unsupported").hidden = false;
  $("connect").disabled = true;
}

// --- Release ---------------------------------------------------------------

fetch("firmware/manifest.json")
  .then((r) => (r.ok ? r.json() : Promise.reject()))
  .then((m) => ($("version-line").textContent = `Installs YUCCA ${m.version}.`))
  .catch(() => ($("version-line").textContent = "No release published yet."));

// --- Serial ----------------------------------------------------------------

let port = null;
let reader = null;
let buffer = "";
const waiters = [];

async function readLoop() {
  const decoder = new TextDecoder();
  while (port && port.readable) {
    reader = port.readable.getReader();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let nl;
        while ((nl = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, nl).replace(/\r$/, "");
          buffer = buffer.slice(nl + 1);
          for (const w of [...waiters]) w(line);
        }
      }
    } catch (e) {
      break;
    } finally {
      reader.releaseLock();
    }
  }
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
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    // Keep the board running: don't hold EN/BOOT through DTR/RTS.
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  } catch (e) {
    setStatus(`Could not open the port: ${e.message}`);
    port = null;
    return;
  }
  readLoop();
  setStatus("Connected, asking YUCCA…");
  await refresh(5);
}

async function refresh(tries = 1) {
  for (let i = 0; i < tries; i++) {
    try {
      const info = await command("yucca_info", infoLine);
      render(info);
      return;
    } catch (e) {
      // The board may still be booting.
    }
  }
  setStatus("No answer. Is YUCCA installed on this board? Press its EN/RESET button and connect again.");
}

function setStatus(text) {
  $("status").textContent = text;
}

// --- Settings form -----------------------------------------------------------

function render(info) {
  const consoleName = labelFor(info.settings.target, info.target);
  setStatus(
    `YUCCA ${info.fw} · ${consoleName} · controller ${info.controller ? "connected" : "not connected"} · ` +
      `console ${info.console ? "connected" : info.paired ? "paired" : "not paired"}`
  );
  const form = $("settings");
  form.replaceChildren();
  for (const [key, spec] of Object.entries(info.settings)) {
    form.append(field(key, spec));
  }
  form.hidden = false;
  $("device").hidden = false;
}

function labelFor(spec, value) {
  const c = (spec.choices || []).find((c) => c.value === value);
  return c ? c.label : value;
}

function field(key, spec) {
  if (spec.type === "int" && spec.min === 0 && spec.max === 1) {
    const label = document.createElement("label");
    label.className = "check";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = spec.value === "1";
    input.addEventListener("change", () => save(key, input.checked ? "1" : "0", spec));
    label.append(input, spec.label);
    return label;
  }
  const label = document.createElement("label");
  label.textContent = spec.label;
  const select = document.createElement("select");
  const choices =
    spec.type === "choice"
      ? spec.choices
      : Array.from({ length: spec.max - spec.min + 1 }, (_, i) => ({ value: String(spec.min + i), label: String(spec.min + i) }));
  for (const c of choices) {
    const opt = new Option(c.label, c.value, false, c.value === spec.value);
    select.append(opt);
  }
  select.addEventListener("change", () => save(key, select.value, spec));
  label.append(select);
  return label;
}

async function save(key, value, spec) {
  setStatus(`Saving ${spec.label}…`);
  try {
    const res = await command(`yucca_set ${key} ${value}`, okLine);
    if (!res.ok) {
      setStatus(`Not saved: ${res.text}`);
      return;
    }
    if (spec.restart) {
      setStatus("Saved. YUCCA is restarting…");
      setTimeout(() => refresh(10), 2500);
    } else {
      await refresh();
    }
  } catch (e) {
    setStatus(e.message);
  }
}

$("connect").addEventListener("click", connect);
$("sync").addEventListener("click", async () => {
  await command("yucca_sync", okLine).catch(() => {});
  setStatus("Pairing window open: start pairing on the console now.");
});
$("forget").addEventListener("click", async () => {
  if (!confirm("Forget the console and all controllers?")) return;
  await command("yucca_forget", okLine).catch(() => {});
  await refresh();
});

// --- Support matrix ----------------------------------------------------------

const ICON = {
  verified: ["✅", "Verified on hardware"],
  "works-in-ci": ["🧪", "Built and tested, not yet verified on hardware"],
  "in-progress": ["🚧", "In progress"],
  planned: ["⬜", "Planned"],
  blocked: ["⛔", "Blocked"],
  "post-v1": ["—", "After v1"],
};

fetch("support.json")
  .then((r) => r.json())
  .then((data) => {
    const cells = new Map(data.cells.map((c) => [`${c.controller}/${c.console}`, c]));
    const table = document.createElement("table");
    const head = table.createTHead().insertRow();
    head.append(Object.assign(document.createElement("th"), { textContent: "Controller" }));
    for (const con of data.consoles) {
      head.append(Object.assign(document.createElement("th"), { textContent: con.name }));
    }
    const body = table.createTBody();
    for (const ctrl of data.controllers) {
      const row = body.insertRow();
      row.insertCell().textContent = ctrl.name;
      for (const con of data.consoles) {
        const cell = cells.get(`${ctrl.id}/${con.id}`);
        const status = cell ? cell.status : con.milestone === "v1" ? "planned" : "post-v1";
        const td = row.insertCell();
        td.textContent = ICON[status][0];
        td.title = ICON[status][1] + (cell && cell.notes ? `: ${cell.notes}` : "");
      }
    }
    const wrap = document.createElement("div");
    wrap.className = "table-wrap";
    wrap.append(table);
    const legend = document.createElement("p");
    legend.className = "hint";
    legend.textContent = Object.values(ICON)
      .map(([i, t]) => `${i} ${t}`)
      .join(" · ");
    $("matrix").replaceChildren(wrap, legend);
  })
  .catch(() => ($("matrix").innerHTML = '<p class="hint">Could not load the support matrix.</p>'));
