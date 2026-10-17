// Plain-language pairing steps per controller and console. Source:
// docs/controllers.md and docs/hw-tests/*.md; keep them in sync.
// `hot` names the parts art.js highlights. `select`/`start`/`home` name the buttons
// YUCCA's shortcuts use, as printed on that controller.

const CONTROLLERS_MD = "https://github.com/MaxLiebe/yucca/blob/main/docs/controllers.md";

export const CONTROLLERS = {
  ds3: {
    short: "PS3",
    select: "Select",
    home: "PS",
    start: "Start",
    hot: ["home"],
    pair: [
      "The PlayStation 3 controller needs a one-time extra step with a USB cable, so it learns YUCCA's address.",
      `Follow <a href="${CONTROLLERS_MD}">the PS3 controller guide</a>, then press the PS button.`,
    ],
  },
  ds4: {
    short: "DualShock 4",
    select: "Share",
    home: "PS",
    start: "Options",
    hot: ["select", "home"],
    pair: ["Hold <b>Share</b> and the <b>PS</b> button together until the light bar flashes quickly."],
  },
  dualsense: {
    short: "DualSense",
    select: "Create",
    home: "PS",
    start: "Options",
    hot: ["select", "home"],
    pair: ["Hold <b>Create</b> and the <b>PS</b> button together until the light around the touchpad flashes quickly."],
  },
  wiimote: {
    short: "Wii Remote",
    select: "−",
    home: "Home",
    start: "+",
    hot: ["sync"],
    pair: ["Take off the battery cover and press the small red <b>SYNC</b> button."],
  },
  wiiu_pro: {
    short: "Wii U Pro",
    select: "−",
    home: "Home",
    start: "+",
    hot: ["sync"],
    pair: ["Press the small <b>SYNC</b> button on the back, next to the charging port."],
  },
  switch_pro: {
    short: "Switch Pro",
    select: "−",
    home: "Home",
    start: "+",
    hot: ["sync"],
    pair: ["Hold the small round <b>sync</b> button on the top, next to the cable port, until the lights run back and forth."],
  },
  joycon: {
    short: "Joy-Con",
    select: "−",
    home: "Home",
    start: "+",
    hot: ["sync"],
    pair: [
      "Hold the small round <b>sync</b> button on the flat side that slides onto the Switch.",
      "Use one Joy-Con at a time for now.",
    ],
  },
  joycon2: {
    short: "Joy-Con 2",
    select: "−",
    home: "Home",
    start: "+",
    hot: ["sync"],
    pair: ["Switch 2 Joy-Cons aren't supported yet."],
  },
  "8bitdo": {
    short: "8BitDo",
    select: "Select",
    home: "Home",
    start: "Start",
    hot: ["sync"],
    pair: [
      "Turn it on by holding <b>Start</b> and <b>Y</b>. This puts it in Switch mode.",
      "Then hold the <b>Pair</b> button for 3 seconds until the light flashes.",
    ],
  },
  stadia: {
    short: "Stadia",
    select: "Options",
    home: "Stadia",
    start: "Menu",
    hot: ["home"],
    pair: [
      `First switch the controller to Bluetooth with <a href="https://stadia.google.com/controller">Google's Stadia tool</a>. You only do this once.`,
      "Then hold the <b>Stadia</b> button and <b>Y</b> until the light flashes.",
    ],
  },
};

export const CONSOLES = {
  wii: {
    pair: [
      "Open the small door on the front of the Wii, where the SD card goes.",
      "Press the red <b>SYNC</b> button inside.",
    ],
    seconds: 20,
  },
  wiiu: {
    pair: [
      "Press the <b>SYNC</b> button on the front of the Wii U, below the power button.",
      "Or, on the GamePad: <b>Controller Settings</b>, then <b>Pair</b>.",
    ],
    seconds: 20,
  },
  switch: {
    pair: [
      "On the Switch, open <b>Controllers</b> from the HOME menu, then <b>Change Grip/Order</b>.",
      "When the screen asks you to, press <b>L</b> and <b>R</b> together on your controller.",
    ],
    seconds: 30,
  },
  switch2: {
    pair: [
      "On the Switch 2, open <b>Controllers</b> from the HOME menu, then <b>Change Grip and Order</b>.",
      "When the screen asks you to, press <b>L</b> and <b>R</b> together on your controller.",
    ],
    seconds: 30,
  },
};

// Friendlier names and a short explanation for each firmware setting. A
// setting not listed here shows with the firmware's own label.
export const SETTINGS = {
  face_layout: {
    label: "Button layout",
    help: "A and B swap places between Nintendo and Xbox-style controllers. Choose whether YUCCA follows where a button is, or the letter printed on it.",
  },
  ble: {
    label: "Look for Stadia and Xbox controllers",
    help: "These connect a different way. Leave it on unless you're told otherwise.",
  },
  wii_mirror: {
    label: "Work in the Wii Menu",
    help: "Also presses the matching Wii Remote buttons, so menus that only listen to a Wii Remote respond.",
  },
  wiiu_mode: {
    label: "Use Wii Remote mode",
    help: "For Virtual Console and other games that don't take a Pro Controller.",
  },
};
