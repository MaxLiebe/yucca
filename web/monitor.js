// The serial monitor: a side panel showing everything the board prints.
// One click on "Serial monitor" opens it and connects to the board.

import * as serial from "./serial.js";

const $ = (id) => document.getElementById(id);
const MAX_LINES = 2000;
const ANSI = /\x1b\[[0-9;]*m/g;
const LEVEL = { E: "error", W: "warn", I: "info", D: "debug", V: "debug" };

// The setup screens ask the board for its status every few seconds; keep
// those questions and answers out of the log unless the visitor asked.
let quietUntil = 0;
export const quietFor = (ms) => (quietUntil = Date.now() + ms);
const isPoll = (line) => /yucca_info|YUCCA \{/.test(line);

let lines = [];

function add(text, kind = "") {
  const log = $("monitor-log");
  const stick = log.scrollHeight - log.scrollTop - log.clientHeight < 24;
  const row = document.createElement("div");
  row.className = `line${kind ? ` ${kind}` : ""}`;
  row.textContent = text || " ";
  log.append(row);
  lines.push(text);
  if (lines.length > MAX_LINES) {
    lines = lines.slice(-MAX_LINES);
    log.firstChild.remove();
  }
  if (stick) log.scrollTop = log.scrollHeight;
}

function setStatus() {
  const open = serial.isOpen();
  $("monitor-status").textContent = open ? "Connected, 115200 baud" : "Not connected";
  $("monitor").classList.toggle("connected", open);
  $("monitor-restart").disabled = !open;
  $("monitor-input").disabled = !open;
}

async function openMonitor() {
  $("monitor").hidden = false;
  $("monitor-open").setAttribute("aria-expanded", "true");
  requestAnimationFrame(() => $("monitor").classList.add("shown"));
  $("monitor-close").focus();
  if (!serial.supported) return add("The serial monitor needs Chrome or Edge on a computer.", "note");
  if (serial.isOpen()) return $("monitor-input").focus();
  try {
    await serial.open(true);
    add("Connected. Press Restart board to see it start up.", "note");
    $("monitor-input").focus();
  } catch (e) {
    add(
      e.name === "NotFoundError"
        ? "No board picked. Click Serial monitor again and choose your board."
        : "Couldn't open the board. Close other apps or tabs using it and try again.",
      "note"
    );
  }
}

function closeMonitor() {
  $("monitor").classList.remove("shown");
  $("monitor-open").setAttribute("aria-expanded", "false");
  setTimeout(() => ($("monitor").hidden = true), 200);
  $("monitor-open").focus();
}

export function initMonitor() {
  serial.onLine((raw) => {
    const text = raw.replace(ANSI, "");
    if (Date.now() < quietUntil && isPoll(text)) return;
    const m = text.match(/^([EWIDV]) \(\d+\)/);
    add(text, m ? LEVEL[m[1]] : "");
  });
  serial.onState((open) => {
    setStatus();
    if (!open && lines.length) add("Disconnected.", "note");
  });
  setStatus();

  $("monitor-open").addEventListener("click", () => ($("monitor").hidden ? openMonitor() : closeMonitor()));
  $("monitor-close").addEventListener("click", closeMonitor);
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !$("monitor").hidden && !document.querySelector("dialog[open]")) closeMonitor();
  });
  $("monitor-clear").addEventListener("click", () => {
    lines = [];
    $("monitor-log").replaceChildren();
  });
  $("monitor-copy").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(lines.join("\n"));
      $("monitor-copy").textContent = "Copied";
    } catch (e) {
      $("monitor-copy").textContent = "Couldn't copy";
    }
    setTimeout(() => ($("monitor-copy").textContent = "Copy"), 1500);
  });
  $("monitor-restart").addEventListener("click", async () => {
    add("Restarting the board…", "note");
    await serial.restart().catch(() => add("Couldn't restart it. Press the EN button on the board.", "note"));
  });
  $("monitor-send").addEventListener("submit", async (e) => {
    e.preventDefault();
    const input = $("monitor-input");
    const cmd = input.value.trim();
    if (!cmd) return;
    quietUntil = 0;
    add(`> ${cmd}`, "sent");
    input.value = "";
    await serial.write(cmd).catch(() => add("Not connected.", "note"));
  });
}
