// Line illustrations of the boards, controllers and consoles YUCCA works with.
// Original drawings, not brand artwork. Every function returns an SVG string;
// `hot` lists the parts to highlight (the button to press, the port to use).
// Colours come from CSS: .ink outlines, .soft fills, .hot the highlighted part.

// Highlight rings go last so the drawing never hides them.
const svg = (w, h, body, label) => {
  const rings = [];
  const rest = body.replace(/<(circle|rect) class="ring"[^>]*\/>/g, (m) => (rings.push(m), ""));
  return `<svg viewBox="0 0 ${w} ${h}" role="img" aria-label="${label}" class="art">${rest}${rings.join("")}</svg>`;
};

// A highlighted part: the part itself plus a ring around it.
const ring = (cx, cy, r) => `<circle class="ring" cx="${cx}" cy="${cy}" r="${r + 7}"/>`;

// --- Controllers (200 x 130) -------------------------------------------------

function playstation(kind, hot = []) {
  const touchpad =
    kind === "ds3"
      ? `<rect class="ink detail" x="88" y="44" width="24" height="8" rx="4"/>`
      : `<rect class="ink soft2" x="72" y="30" width="56" height="30" rx="6"/>`;
  const select = kind === "ds3" ? [86, 56] : [64, 34];
  const ps = [100, 80];
  const parts = `
    <path class="ink soft" d="M48 26 C70 20 130 20 152 26 C172 31 182 44 186 62 L194 100 C197 116 186 124 175 120 C164 116 156 102 148 92 C140 86 60 86 52 92 C44 102 36 116 25 120 C14 124 3 116 6 100 L14 62 C18 44 28 31 48 26 Z"/>
    ${touchpad}
    <g class="ink detail">
      <path d="M38 50 h8 v-8 h8 v8 h8 v8 h-8 v8 h-8 v-8 h-8 Z"/>
      <circle cx="152" cy="42" r="5.5"/><circle cx="152" cy="66" r="5.5"/>
      <circle cx="140" cy="54" r="5.5"/><circle cx="164" cy="54" r="5.5"/>
    </g>
    <circle class="ink soft2" cx="72" cy="78" r="12"/><circle class="ink soft2" cx="128" cy="78" r="12"/>
    <rect class="ink detail ${hot.includes("select") ? "hot" : ""}" x="${select[0] - 4}" y="${select[1] - 3}" width="8" height="6" rx="3"/>
    <rect class="ink detail" x="${200 - select[0] - 4}" y="${select[1] - 3}" width="8" height="6" rx="3"/>
    <circle class="ink detail ${hot.includes("home") ? "hot" : ""}" cx="${ps[0]}" cy="${ps[1]}" r="5"/>`;
  const rings = [hot.includes("select") ? ring(...select, 4) : "", hot.includes("home") ? ring(...ps, 5) : ""].join("");
  return svg(200, 130, rings + parts, "Controller");
}

function pro(kind, hot = []) {
  // Wii U Pro has both sticks up top; the others put the left stick up, right stick down.
  const top = kind === "wiiu_pro";
  const ls = top ? [62, 46] : [56, 50];
  const rs = top ? [138, 46] : [124, 80];
  const dpad = top ? [70, 78] : [76, 80];
  const face = top ? [130, 78] : [146, 50];
  const sync = kind === "wiiu_pro" ? [100, 96] : [100, 22];
  const parts = `
    <path class="ink soft" d="M50 24 C75 18 125 18 150 24 C172 30 182 44 186 64 L192 98 C195 116 183 124 171 119 C160 114 152 100 144 92 C132 86 68 86 56 92 C48 100 40 114 29 119 C17 124 5 116 8 98 L14 64 C18 44 28 30 50 24 Z"/>
    <circle class="ink soft2" cx="${ls[0]}" cy="${ls[1]}" r="12"/>
    <circle class="ink soft2" cx="${rs[0]}" cy="${rs[1]}" r="12"/>
    <path class="ink detail" d="M${dpad[0] - 12} ${dpad[1] - 4} h8 v-8 h8 v8 h8 v8 h-8 v8 h-8 v-8 h-8 Z"/>
    <g class="ink detail">
      <circle cx="${face[0]}" cy="${face[1] - 11}" r="5"/><circle cx="${face[0]}" cy="${face[1] + 11}" r="5"/>
      <circle cx="${face[0] - 11}" cy="${face[1]}" r="5"/><circle cx="${face[0] + 11}" cy="${face[1]}" r="5"/>
    </g>
    <circle class="ink detail ${hot.includes("select") ? "hot" : ""}" cx="86" cy="44" r="3.5"/>
    <circle class="ink detail" cx="114" cy="44" r="3.5"/>
    <circle class="ink detail ${hot.includes("home") ? "hot" : ""}" cx="100" cy="60" r="5"/>
    ${kind === "wiiu_pro" ? "" : `<rect class="ink detail" x="92" y="17" width="16" height="5" rx="2"/>`}
    <circle class="ink detail ${hot.includes("sync") ? "hot" : ""}" cx="${sync[0] + (kind === "wiiu_pro" ? 0 : 16)}" cy="${sync[1]}" r="3"/>`;
  const syncAt = [sync[0] + (kind === "wiiu_pro" ? 0 : 16), sync[1]];
  const rings = [
    hot.includes("sync") ? ring(...syncAt, 3) : "",
    hot.includes("select") ? ring(86, 44, 3.5) : "",
    hot.includes("home") ? ring(100, 60, 5) : "",
  ].join("");
  return svg(200, 130, rings + parts, "Controller");
}

// Seen from the back with the battery cover off, so the SYNC button shows.
function wiimote(hot = []) {
  const back = hot.includes("sync");
  if (back) {
    return svg(
      200,
      130,
      `${ring(100, 92, 4)}
      <rect class="ink soft" x="78" y="4" width="44" height="122" rx="16"/>
      <rect class="ink soft2" x="84" y="40" width="32" height="70" rx="6"/>
      <rect class="ink detail" x="88" y="46" width="11" height="40" rx="4"/>
      <rect class="ink detail" x="101" y="46" width="11" height="40" rx="4"/>
      <circle class="ink hot" cx="100" cy="92" r="4"/>`,
      "Wii Remote seen from the back, battery cover off"
    );
  }
  return svg(
    200,
    130,
    `${hot.includes("home") ? ring(100, 66, 4.5) : ""}${hot.includes("select") ? ring(88, 66, 3.5) : ""}
    <rect class="ink soft" x="78" y="4" width="44" height="122" rx="16"/>
    <path class="ink detail" d="M94 18 h6 v-6 h0 h6 v6 h6 v6 h-6 v6 h-6 v-6 h-6 Z" transform="translate(-3 0)"/>
    <circle class="ink soft2" cx="100" cy="46" r="7"/>
    <circle class="ink detail ${hot.includes("select") ? "hot" : ""}" cx="88" cy="66" r="3.5"/>
    <circle class="ink detail ${hot.includes("home") ? "hot" : ""}" cx="100" cy="66" r="4.5"/>
    <circle class="ink detail" cx="112" cy="66" r="3.5"/>
    <circle class="ink detail" cx="100" cy="90" r="5"/><circle class="ink detail" cx="100" cy="104" r="5"/>
    <g class="detail-fill"><rect x="88" y="117" width="4" height="3" rx="1"/><rect x="95" y="117" width="4" height="3" rx="1"/><rect x="102" y="117" width="4" height="3" rx="1"/><rect x="109" y="117" width="4" height="3" rx="1"/></g>`,
    "Wii Remote"
  );
}

// A pair of Joy-Cons, side by side; the sync button sits on the left one's rail.
function joycon(two, hot = []) {
  const top = two ? 4 : 8;
  const bot = two ? 126 : 122;
  const r = two ? 26 : 22;
  const half = (inner) => `
    <path class="ink soft" d="M96 ${top} L96 ${bot} L${50 + r} ${bot} C58 ${bot} 50 ${bot - 8} 50 ${bot - r} L50 ${top + r} C50 ${top + 8} 58 ${top} ${50 + r} ${top} Z"/>
    <rect class="ink soft2" x="92" y="${top + 8}" width="6" height="${bot - top - 16}" rx="2"/>
    ${inner}`;
  const left = half(`
    <circle class="ink detail ${hot.includes("select") ? "hot" : ""}" cx="84" cy="${top + 10}" r="3"/>
    <circle class="ink soft2" cx="72" cy="40" r="10"/>
    <g class="ink detail"><circle cx="72" cy="64" r="4.5"/><circle cx="72" cy="84" r="4.5"/><circle cx="62" cy="74" r="4.5"/><circle cx="82" cy="74" r="4.5"/></g>
    <circle class="ink detail ${hot.includes("sync") ? "hot" : ""}" cx="95" cy="66" r="3"/>`);
  const right = half(`
    <circle class="ink detail" cx="84" cy="${top + 10}" r="3"/>
    <g class="ink detail"><circle cx="72" cy="30" r="4.5"/><circle cx="72" cy="50" r="4.5"/><circle cx="62" cy="40" r="4.5"/><circle cx="82" cy="40" r="4.5"/></g>
    <circle class="ink soft2" cx="72" cy="76" r="10"/>
    <circle class="ink detail ${hot.includes("home") ? "hot" : ""}" cx="80" cy="100" r="4"/>`);
  return svg(
    200,
    130,
    `${hot.includes("sync") ? ring(95, 66, 3) : ""}${hot.includes("select") ? ring(84, top + 10, 3) : ""}
    ${left}<g transform="translate(${two ? 204 : 202} 0) scale(-1 1)">${right}</g>`,
    two ? "Switch 2 Joy-Con controllers" : "Joy-Con controllers"
  );
}

const CONTROLLER_ART = {
  ds3: (hot) => playstation("ds3", hot),
  ds4: (hot) => playstation("ds4", hot),
  dualsense: (hot) => playstation("dualsense", hot),
  wiimote: (hot) => wiimote(hot),
  wiiu_pro: (hot) => pro("wiiu_pro", hot),
  switch_pro: (hot) => pro("switch_pro", hot),
  joycon: (hot) => joycon(false, hot),
  joycon2: (hot) => joycon(true, hot),
  "8bitdo": (hot) => pro("8bitdo", hot),
  stadia: (hot) => pro("stadia", hot),
};

export function controllerArt(id, hot = []) {
  return (CONTROLLER_ART[id] || CONTROLLER_ART.switch_pro)(hot);
}

// --- Consoles (200 x 130) ----------------------------------------------------

function wii(hot) {
  return svg(
    200,
    130,
    `${hot ? ring(108, 98, 3.5) : ""}
    <path class="ink soft2" d="M70 122 L130 122 L124 112 L76 112 Z"/>
    <rect class="ink soft" x="84" y="6" width="32" height="108" rx="4"/>
    <line class="ink" x1="100" y1="16" x2="100" y2="80"/>
    <rect class="ink ${hot ? "soft2" : "detail"}" x="91" y="88" width="22" height="18" rx="2"/>
    <circle class="ink detail ${hot ? "hot" : ""}" cx="108" cy="98" r="3.5"/>`,
    hot ? "Wii with the SD card door open and the SYNC button showing" : "Wii"
  );
}

function wiiu(hot) {
  return svg(
    200,
    130,
    `${hot ? ring(160, 102, 3.5) : ""}
    <rect class="ink soft" x="14" y="62" width="172" height="52" rx="12"/>
    <line class="ink" x1="40" y1="80" x2="140" y2="80"/>
    <circle class="ink detail" cx="160" cy="84" r="5"/>
    <circle class="ink detail ${hot ? "hot" : ""}" cx="160" cy="102" r="3.5"/>
    <rect class="ink soft" x="56" y="8" width="88" height="44" rx="12"/>
    <rect class="ink soft2" x="74" y="14" width="52" height="32" rx="2"/>`,
    hot ? "Wii U with the SYNC button below the power button" : "Wii U with its GamePad"
  );
}

function nswitch(two, screen) {
  const w = two ? 190 : 168;
  const x = (200 - w) / 2;
  const jc = two ? 25 : 24;
  const r = two ? 22 : 18;
  const top = two ? 16 : 22;
  const bot = two ? 114 : 108;
  const text = screen
    ? `<g class="screen-text"><text x="100" y="54" text-anchor="middle">Controllers</text><text x="100" y="72" text-anchor="middle" class="hot-text">${screen}</text></g>`
    : "";
  return svg(
    200,
    130,
    `<path class="ink soft" d="M${x + jc} ${top} L${x + jc} ${bot} L${x + r} ${bot} C${x + 4} ${bot} ${x} ${bot - 6} ${x} ${bot - r} L${x} ${top + r} C${x} ${top + 6} ${x + 4} ${top} ${x + r} ${top} Z"/>
    <path class="ink soft" d="M${x + w - jc} ${top} L${x + w - jc} ${bot} L${x + w - r} ${bot} C${x + w - 4} ${bot} ${x + w} ${bot - 6} ${x + w} ${bot - r} L${x + w} ${top + r} C${x + w} ${top + 6} ${x + w - 4} ${top} ${x + w - r} ${top} Z"/>
    <rect class="ink soft" x="${x + jc}" y="${top}" width="${w - 2 * jc}" height="${bot - top}" rx="2"/>
    <rect class="screen" x="${x + jc + (two ? 4 : 6)}" y="${top + (two ? 4 : 6)}" width="${w - 2 * jc - (two ? 8 : 12)}" height="${bot - top - (two ? 8 : 12)}" rx="2"/>
    ${text}
    <circle class="ink soft2" cx="${x + jc / 2}" cy="${top + 22}" r="7"/>
    <circle class="ink soft2" cx="${x + w - jc / 2}" cy="${bot - 30}" r="7"/>
    <g class="ink detail"><circle cx="${x + jc / 2}" cy="${bot - 34}" r="3"/><circle cx="${x + jc / 2}" cy="${bot - 22}" r="3"/></g>
    <g class="ink detail"><circle cx="${x + w - jc / 2}" cy="${top + 22}" r="3"/><circle cx="${x + w - jc / 2}" cy="${top + 34}" r="3"/></g>`,
    two ? "Nintendo Switch 2" : "Nintendo Switch"
  );
}

const CONSOLE_ART = {
  wii: (hot) => wii(hot),
  wiiu: (hot) => wiiu(hot),
  switch: (hot) => nswitch(false, hot ? "Change Grip/Order" : ""),
  switch2: (hot) => nswitch(true, hot ? "Change Grip and Order" : ""),
};

export function consoleArt(id, hot = false) {
  return (CONSOLE_ART[id] || CONSOLE_ART.switch)(hot);
}

// --- The board, the computer, and the three together ------------------------

// An ESP32 dev board, USB port on the right. `hot`: "usb" or "boot".
export function boardArt(hot = "") {
  const pins = (y) =>
    Array.from({ length: 15 }, (_, i) => `<rect x="${30 + i * 9.5}" y="${y}" width="4" height="4" rx="1"/>`).join("");
  return svg(
    200,
    130,
    `${hot === "boot" ? ring(156, 74, 4) : ""}${hot === "usb" ? `<rect class="ring" x="168" y="52" width="30" height="26" rx="8"/>` : ""}
    <rect class="ink soft" x="22" y="36" width="158" height="58" rx="5"/>
    <g class="detail-fill">${pins(40)}${pins(86)}</g>
    <rect class="ink soft2" x="30" y="50" width="62" height="30" rx="2"/>
    <path class="ink" d="M36 56 h8 v18 h8 v-18 h8 v18 h8 v-18" fill="none"/>
    <rect class="ink detail" x="100" y="56" width="18" height="18" rx="2"/>
    <circle class="ink detail" cx="156" cy="56" r="4"/>
    <circle class="ink detail ${hot === "boot" ? "hot" : ""}" cx="156" cy="74" r="4"/>
    <rect class="ink ${hot === "usb" ? "hot" : "detail"}" x="172" y="57" width="18" height="16" rx="3"/>
    <g class="label"><text x="148" y="59" text-anchor="end">EN</text><text x="148" y="77" text-anchor="end">BOOT</text></g>`,
    hot === "boot" ? "ESP32 board with the BOOT button highlighted" : "ESP32 board"
  );
}

export function laptopArt() {
  return svg(
    260,
    130,
    `<rect class="ink soft" x="20" y="12" width="128" height="84" rx="6"/>
    <rect class="screen" x="28" y="20" width="112" height="68" rx="2"/>
    <path class="ink soft2" d="M6 104 L162 104 L154 96 L14 96 Z"/>
    <path class="cable" d="M162 92 C180 92 172 67 186 67"/>
    <g transform="translate(254 43) scale(-0.36 0.36)">
      <rect class="ink soft" x="22" y="36" width="158" height="58" rx="5"/>
      <rect class="ink soft2" x="30" y="50" width="62" height="30" rx="2"/>
      <rect class="ink detail" x="172" y="57" width="18" height="16" rx="3"/>
    </g>`,
    "A computer connected to the ESP32 board with a USB cable"
  );
}
