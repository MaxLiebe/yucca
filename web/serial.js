// One Web Serial connection to the board, shared by the setup screens, the
// installer and the serial monitor.

const encoder = new TextEncoder();
export const supported = "serial" in navigator;

let port = null; // open SerialPort, or null
let known = null; // last port the visitor picked, open or not
let reader = null;
let loop = null;
let closing = false;
let buffer = "";
const lineListeners = new Set();
const stateListeners = new Set();

export const isOpen = () => port !== null;
export const onLine = (fn) => lineListeners.add(fn);
export const onState = (fn) => stateListeners.add(fn);
const emitState = () => stateListeners.forEach((fn) => fn(isOpen()));

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
          for (const fn of [...lineListeners]) {
            try {
              fn(line);
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

// A port to use: the open one, the one picked before, the only one this site
// was ever allowed to use, or (when `ask`) the browser's picker.
export async function choosePort(ask = true) {
  if (port) return port;
  if (known) return known;
  const granted = supported ? await navigator.serial.getPorts() : [];
  if (granted.length === 1) return (known = granted[0]);
  if (!ask) return null;
  return (known = await navigator.serial.requestPort());
}

// Open the board's console. Throws with .name "NotFoundError" when the
// visitor closes the picker without choosing.
export async function open(ask = true) {
  if (port) return;
  const p = await choosePort(ask);
  if (!p) return;
  await p.open({ baudRate: 115200 });
  // Keep the board running: don't hold EN/BOOT through DTR/RTS.
  await p.setSignals({ dataTerminalReady: false, requestToSend: false });
  port = p;
  loop = readLoop(p);
  emitState();
}

// Close the port so the installer (or another open) can use it.
export async function close() {
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
  emitState();
}

// Hand the port to the installer: closed, but remembered for reopening.
export async function release() {
  const p = port || known;
  await close();
  return p;
}
export const remember = (p) => (known = p);

export async function write(text) {
  if (!port || !port.writable) throw new Error("Not connected");
  const writer = port.writable.getWriter();
  try {
    await writer.write(encoder.encode(text + "\r\n"));
  } finally {
    writer.releaseLock();
  }
}

// Send a console command; resolve with the first line `match` accepts.
export function command(cmd, match, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    const onLineFn = (line) => {
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
      lineListeners.delete(onLineFn);
    };
    lineListeners.add(onLineFn);
    write(cmd).catch((e) => {
      done();
      reject(e);
    });
  });
}

// Pulse EN through RTS, like the board's reset button.
export async function restart() {
  if (!port) throw new Error("Not connected");
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await new Promise((r) => setTimeout(r, 120));
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
}

export const infoLine = (line) => {
  const i = line.indexOf("YUCCA {");
  return i >= 0 ? JSON.parse(line.slice(i + 6)) : undefined;
};
export const okLine = (line) => {
  const m = line.match(/^(?:.*>\s*)?(OK|ERR)\b ?(.*)$/);
  return m ? { ok: m[1] === "OK", text: m[2] } : undefined;
};

if (supported) {
  navigator.serial.addEventListener("disconnect", (e) => {
    if (e.target === port) close();
    if (e.target === known) known = null;
  });
}
