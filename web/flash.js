// Install the firmware from firmware/manifest.json onto the board, in the
// page. Follows esp-web-tools' flash.ts (same esptool-js major) so the
// steps and resets match what worked before.

import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.6.1/bundle.js";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// esp-web-tools' hard reset: hold RTS, then the chip's own reset sequence.
async function hardReset(transport, loader) {
  try {
    await transport.setRTS(true);
    await sleep(100);
    await loader.after();
  } catch (e) {
    // The board may already be gone.
  }
}

export class FlashError extends Error {
  constructor(kind, message) {
    super(message);
    this.kind = kind;
  }
}

// `onStep({ step, percent })`, step one of: connecting, checking,
// downloading, erasing, writing, restarting.
export async function flash(port, manifest, { erase, onStep }) {
  const transport = new Transport(port);
  const loader = new ESPLoader({ transport, baudrate: 115200, enableTracing: false });
  const finish = async () => {
    await hardReset(transport, loader);
    await transport.disconnect().catch(() => {});
  };

  onStep({ step: "connecting" });
  try {
    await loader.main();
    await loader.flashId();
  } catch (e) {
    await finish();
    throw new FlashError("connect", e.message);
  }

  onStep({ step: "checking" });
  const chip = loader.chip.CHIP_NAME;
  const build = manifest.builds.find((b) => b.chipFamily === chip);
  if (!build) {
    await finish();
    throw new FlashError("chip", chip);
  }

  onStep({ step: "downloading" });
  const files = [];
  let total = 0;
  try {
    for (const part of build.parts) {
      const resp = await fetch(new URL(part.path, new URL("firmware/", location.href)));
      if (!resp.ok) throw new Error(`${part.path}: ${resp.status}`);
      const data = new Uint8Array(await resp.arrayBuffer());
      files.push({ data, address: part.offset });
      total += data.length;
    }
  } catch (e) {
    await finish();
    throw new FlashError("download", e.message);
  }

  if (erase) {
    onStep({ step: "erasing" });
    try {
      await loader.eraseFlash();
    } catch (e) {
      await finish();
      throw new FlashError("write", e.message);
    }
  }

  onStep({ step: "writing", percent: 0 });
  let done = 0;
  try {
    await loader.writeFlash({
      fileArray: files,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (i, written, size) => {
        const uncompressed = (written / size) * files[i].data.length;
        if (written === size) {
          done += uncompressed;
          return;
        }
        onStep({ step: "writing", percent: Math.floor(((done + uncompressed) / total) * 100) });
      },
    });
  } catch (e) {
    await finish();
    throw new FlashError("write", e.message);
  }

  onStep({ step: "restarting", percent: 100 });
  await finish();
}
