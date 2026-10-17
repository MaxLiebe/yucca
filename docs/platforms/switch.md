# Switch and Switch 2 (Pro Controller emulation)

Research date: 2026-10-17. Scope: an original ESP32 running BTstack (the copy vendored by Bluepad32, BTstack 1.8.3 in `external/bluepad32/external/btstack`) that appears to a Nintendo Switch or Switch 2 as a Switch 1 Pro Controller, while it is also connected as host to a real gamepad.

How to read this doc:
- Byte offsets in report tables count the report ID as byte 0. On the air, every HID interrupt packet also starts with a HIDP header: `0xA1` (DATA|Input) for packets from the device to the console, `0xA2` (DATA|Output) for packets from the console to the device. That header is not counted here.
- "[S#]" refers to the Sources list at the end.
- **UNCERTAIN** marks something no primary source confirms, or where the sources disagree.
- All tables and descriptions here are written fresh from the protocol facts. No GPL code was copied. Byte values quoted from captures are protocol facts. The HID descriptor in §2.3 comes from nxbt (MIT) and is credited there.

---

## 1. Summary

- **Transport.** The device is a BR/EDR HID device. Control uses L2CAP PSM `0x11` and interrupt uses PSM `0x13`. Use the name `Pro Controller` and Class of Device `0x002508` (Peripheral, Gamepad). The SDP HID record has to carry Nintendo's report descriptor. Pairing is SSP "Just Works": IO capability NoInputNoOutput, auto-accept. [S3][S5][S6]
- **First pairing.** It happens only while the console shows **Controllers > Change Grip/Order**. The console inquires and pages us, so we must be discoverable and connectable. On Switch 2 the screen is called "Change Grip and Order" and the steps are the same. [S9][S12]
- **Reconnect.** We page the console: open an ACL link to its stored BD_ADDR, then open PSM `0x11` followed by `0x13`. The link has to authenticate with the link key stored at pairing. [S5][S6]
- **Handshake.** After every connection the console sends output reports `0x01` carrying subcommands. Typical order (from a capture): `02` device info, `08` shipment, `10` SPI reads (`6000`, `603D`), `03 30` set full mode, `04`, more `10` reads (`6080`, `6098`, `8010`, `603D`, `6020`), `48 01`, `40 01`, `48 01`, `30 xx` player lights. Each one gets an input report `0x21` with an ACK. [S4]
- **Steady state.** We stream input report `0x30` (49 bytes: buttons, 12-bit sticks, IMU) about every 15 ms; real Pro Controllers go faster. The console sends `0x10` (rumble only) or `0x01` (rumble plus subcommand). [S1][S5][S6]
- **Biggest risks:**
  1. **PSM collision.** Bluepad32 already registers L2CAP services on PSM `0x11`/`0x13`, so BTstack's `hid_device_init()` will fail with `L2CAP_SERVICE_ALREADY_REGISTERED`. Incoming channels from the Switch and from gamepads must be split by remote address inside one handler.
  2. **Scatternet and role.** Bluepad32 asks to be master on incoming links (`hci_set_master_slave_policy(HCI_ROLE_MASTER)`), but the console expects to be master. Whether the ESP32 controller handles being master to the gamepad and slave to the Switch at the same time, at 8–15 ms report latency, is **UNVERIFIED**.
  3. **Console updates.** Switch firmware 12.0.0 broke emulators for a while. The console became stricter about which controllers it accepts and about report rate on menus. [S10]
  4. **Switch 2 is weakly proven.** Emulator reports are mixed. Switch 1 controllers, including genuine ones, cannot wake a Switch 2 from sleep. [S11][S13][S14]

---

## 2. Transport and pairing

### 2.1 Identity
| Item | Value | Notes |
|---|---|---|
| Local name | `Pro Controller` | On Switch 2, the console only listed an nxbt device after its alias was set to "Pro Controller" [S11]. Genuine Joy-Cons use `Joy-Con (L)` / `Joy-Con (R)`. |
| Class of Device | `0x002508` (Major 0x05 Peripheral, Minor 0x08 Gamepad; no service bits) | nxbt sets `0x02508` [S5]. BlueCubeMod sets major 0x05 and minor 0b000010, which is the same CoD [S7]. In the firmware 12 thread, one user found changing the CoD alone was enough to restore pairing [S10]. |
| BD_ADDR | The ESP32's BT MAC. It must stay **stable** because the console keys pairing on it. | Reports disagree on whether a Nintendo OUI (e.g. `7C:BB:8A`) helps. One user said reusing a known Pro Controller MAC fixed firmware 12; another said it didn't [S10][S11]. **UNCERTAIN**, and we don't recommend spoofing. |
| Device ID (PnP, SDP UUID 0x1200) | Bluepad32 registers DID with vendor = BlueKitchen (BT SIG source), product 1 (`uni_bt_sdp_server_init`) | nxbt runs on BlueZ, which publishes a Linux DID, and the console still accepts it. So the console probably doesn't check DID. **UNCERTAIN**: it may be worth publishing VID `0x057E` / PID `0x2009` (USB source) when in Switch mode. |

### 2.2 SDP HID record (UUID 0x1124)
These attribute values come from the record nxbt publishes [S5]. They map directly onto BTstack's `hid_sdp_record_t` / `hid_create_sdp_record()`.

| Attr | Meaning | Value |
|---|---|---|
| 0x0001 | ServiceClassIDList | HID `0x1124` |
| 0x0004 | Protocol descriptor | L2CAP PSM `0x0011`, HIDP |
| 0x000D | Additional protocol | L2CAP PSM `0x0013`, HIDP |
| 0x0009 | Profile | HID `0x0101` |
| 0x0100/0101/0102 | Name / Description / Provider | "Wireless Gamepad" / "Gamepad" / "Nintendo" |
| 0x0201 | HIDParserVersion | `0x0111` |
| 0x0202 | HIDDeviceSubclass | `0x08` (gamepad) |
| 0x0203 | HIDCountryCode | `0x21` |
| 0x0204 | VirtualCable | true |
| 0x0205 | ReconnectInitiate | true. The device does the reconnecting. |
| 0x0206 | Descriptor list | type `0x22`, bytes in §2.3 |
| 0x0207 | LangIDBase | `0x0409`, `0x0100` |
| 0x0209 / 0x020A | BatteryPower / RemoteWake | true / true |
| 0x020C | SupervisionTimeout | `0x0C80` (3200 slots = 2.0 s) |
| 0x020D / 0x020E | NormallyConnectable / BootDevice | false / false |

### 2.3 HID report descriptor (170 bytes; Nintendo's, as published by nxbt [S5], MIT)
```
05 01 09 05 A1 01 06 01 FF 85 21 09 21 75 08 95
30 81 02 85 30 09 30 75 08 95 30 81 02 85 31 09
31 75 08 96 69 01 81 02 85 32 09 32 75 08 96 69
01 81 02 85 33 09 33 75 08 96 69 01 81 02 85 3F
05 09 19 01 29 10 15 00 25 01 75 01 95 10 81 02
05 01 09 39 15 00 25 07 75 04 95 01 81 42 05 09
75 04 95 01 81 01 05 01 09 30 09 31 09 33 09 34
16 00 00 27 FF FF 00 00 75 10 95 04 81 02 06 01
FF 85 01 09 01 75 08 95 30 91 02 85 10 09 10 75
08 95 30 91 02 85 11 09 11 75 08 95 30 91 02 85
12 09 12 75 08 95 30 91 02 C0
```
What it declares:
- Inputs: `0x21` and `0x30` are 48 data bytes each. `0x31`, `0x32` and `0x33` are 361 bytes each. `0x3F` has 16 buttons, a 4-bit hat and four 16-bit axes.
- Outputs: `0x01`, `0x10`, `0x11` and `0x12` are 48 bytes each.
- In practice, 0x21/0x30 packets are 1 + 48 = 49 bytes, or 50 bytes on the wire with the `0xA1` header. nxbt pads every packet to 50 [S5].

### 2.4 PSMs, channel order, pairing
- **PSMs.** HID Control `0x11`, HID Interrupt `0x13`. Open control first, then interrupt. That is how both joycontrol and nxbt reconnect [S5][S6]. All Switch traffic (subcommands, rumble, input reports) goes over **interrupt**. Control should accept and ignore HIDP HANDSHAKE / SET_PROTOCOL if they ever arrive.
- **Pairing method.** SSP with no user interaction, i.e. "Just Works". BlueZ-based emulators needed an agent that auto-accepts the confirmation, and pairing stalled without one [S11]. In BTstack: `gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT)` and `gap_ssp_set_auto_accept(true)`. Bluepad32 already enables auto-accept. Legacy PIN pairing is not used. **UNCERTAIN** whether the console asks for MITM; Just Works works in practice.
- **Bonding.** A real controller stores the host BD_ADDR and the 128-bit link key in its SPI at 0x2000, with a host-capability byte of `0x68` for a Switch [S2]. We store ours in BTstack's link-key DB (NVS on ESP32). Bluepad32 has code that deletes link keys (`uni_bt_bredr.c`, e.g. when a device is removed). Make sure that path can never delete the console's key.
- **Who pages whom.**
  - First pairing: the console runs inquiry while Change Grip/Order is open and connects to discoverable controllers. "The Switch only pairs with new controllers in the Change Grip/Order menu" [S6]. We must be discoverable plus connectable during this window.
  - Reconnect: the **controller pages the console** and opens 0x11 then 0x13. The SDP record says ReconnectInitiate = true. joycontrol `-r <switch MAC>` and nxbt `reconnect()` do exactly this, and need no menu [S5][S6]. Subcommand 0x06 confirms the real controller has a "page mode" [S2].
  - **UNCERTAIN** whether the console ever pages a bonded controller itself. Plan for it never doing so, but keep page scan enabled anyway.
- **What the console checks on reconnect** (observed facts, not a spec):
  1. The BD_ADDR is in its pairing list, and authentication with the stored link key succeeds. If it fails, re-pair via Change Grip/Order.
  2. The full subcommand handshake (§4) runs again. "Switch always sends `08 00` after every connection" [S2].
  3. Reports keep arriving. If nothing is sent, the console drops the link. nxbt re-sends an unchanged report about every 132 ticks (~1 s) as a keep-alive [S5].
  4. Since firmware 12, report rate on some menus matters (§6) [S10].
- **Disconnect from the console.** It may send subcommand `06 00` (go to sleep / page-scan mode). ACK it, then drop the link [S2].

---

## 3. Reports

### 3.1 Standard input header (shared by 0x21, 0x30, 0x31)
| Byte | Content |
|---|---|
| 0 | Report ID (`0x21` / `0x30` / `0x31`) |
| 1 | Timer. Free-running 8-bit counter that wraps at 0xFF [S1]. joycontrol adds 1 per report sent [S6]. nxbt derives it from elapsed time, with a comment claiming a ~4.96 ms real tick [S5]. **UNCERTAIN** whether the console validates it. A per-report increment works. |
| 2 | Battery and connection. High nibble = battery: 8 full, 6 medium, 4 low, 2 critical, 0 empty; bit 0 of the nibble (`0x10`) = charging. Low nibble = connection info: bits 2..1 = 0 for Pro/Grip and 3 for Joy-Con; bit 0 = powered by Switch/USB [S1]. **Pro over BT, full battery: `0x90`** (nxbt [S5]). Joy-Con captures show `0x8E` [S4]. |
| 3 | Right buttons: `01` Y, `02` X, `04` B, `08` A, `10` SR, `20` SL, `40` R, `80` ZR |
| 4 | Shared buttons: `01` Minus, `02` Plus, `04` R-stick, `08` L-stick, `10` Home, `20` Capture, `40` –, `80` charging-grip |
| 5 | Left buttons: `01` Down, `02` Up, `04` Right, `08` Left, `10` SR, `20` SL, `40` L, `80` ZL |
| 6–8 | Left stick, 12-bit X and Y |
| 9–11 | Right stick, 12-bit X and Y |
| 12 | "Vibrator input report" byte. Observed values `0x70`/`0x80`/`0x90`/`0xA0`/`0xB0`/`0xC0`; its meaning is unknown [S1]. A constant `0x80` is used in captures, joycontrol and BlueCubeMod [S4][S6][S7]. |

For a Pro Controller, leave SL/SR and the charging-grip bit cleared.

**Stick packing (3 bytes per stick).** For 12-bit values X and Y, where 0x000..0xFFF is nominally centred near 0x800:
- `b0 = X & 0xFF`
- `b1 = (X >> 8) & 0x0F | (Y & 0x0F) << 4`
- `b2 = Y >> 4`

To decode: `X = b0 | (b1 & 0xF) << 8`, `Y = (b1 >> 4) | b2 << 4` [S1]. +Y is up. Example: centre 0x800/0x800 packs to `00 08 80`.

### 3.2 Input 0x30: full mode (sent periodically)
| Byte | Content |
|---|---|
| 0–12 | Header from §3.1 |
| 13–24 | IMU sample 1: accel X, Y, Z, then gyro 1, 2, 3; each int16 LE |
| 25–36 | IMU sample 2 (same layout) |
| 37–48 | IMU sample 3 (same layout) |

The three samples are taken about 5 ms apart, oldest first [S3]. If the IMU is disabled, send zeros. Total length is 49 bytes.

### 3.3 Input 0x21: subcommand reply
| Byte | Content |
|---|---|
| 0–12 | Header from §3.1, carrying the **current** buttons and sticks |
| 13 | ACK. Bit 7 = 1 means ACK. Bits 6..0 give the data type: `0x00` for a plain ACK. A NACK is `0x00` [S1]. |
| 14 | Subcommand ID being answered |
| 15–48 | Reply data, up to 34 bytes; pad with zeros |

### 3.4 Input 0x3F: simple HID mode
The device starts in this mode before `03 30`. It sends a report only when the state changes.

| Byte | Content |
|---|---|
| 0 | `0x3F` |
| 1 | `01` B, `02` A, `04` Y, `08` X, `10` L, `20` R, `40` ZL, `80` ZR |
| 2 | `01` Minus, `02` Plus, `04` LS, `08` RS, `10` Home, `20` Capture |
| 3 | Hat: 0 = up, then clockwise to 7 = up-left; 8 = neutral |
| 4–7 | Left X, left Y: uint16 LE |
| 8–11 | Right X, right Y: uint16 LE |

Sources: [S1] for the hat and stick layout. The Pro button bits follow SDL's parser [S8]. The ZL/ZR bits come from the descriptor's 16-button field and are **UNCERTAIN**.

Whether a genuine Pro Controller sends 0x3F before `03 30` over BT is **UNCERTAIN**. joycontrol sends empty input reports once a second after connecting "until the Switch decides to reply" [S6]. On hardware (2026-10-17), a Switch 2 paged YUCCA during Change Grip and Order, completed SSP and opened 0x11/0x13, then sent no subcommand for 15 s while YUCCA sent nothing. So YUCCA now sends a neutral 0x3F report once a second until the console's first output report.

### 3.5 Output 0x01 / 0x10 (console to us)
| Byte | Content |
|---|---|
| 0 | `0x01` (rumble + subcommand) or `0x10` (rumble only). `0x11` (MCU request) and `0x12` are rarely seen; ignore them. |
| 1 | Packet counter, 0x0–0xF wrapping |
| 2–5 | Left actuator rumble (4 bytes) |
| 6–9 | Right actuator rumble (4 bytes) |
| 10 | Subcommand ID (0x01 only) |
| 11–48 | Subcommand arguments |

**Rumble decoding** [S1][S2-rumble]. Each 4-byte group encodes a high band and a low band:
- `00 01 40 40` = neutral, silent.
- High band, 2 bytes:
  - frequency = byte0, plus the LSB of byte1 as the 9th bit;
  - amplitude = `byte1 & 0xFE`, from 0x00 to 0xC8 for 0.0–1.0.
- Low band:
  - frequency = `byte2 & 0x7F`;
  - amplitude = byte3, from 0x40 to 0x72 for 0.0–1.0, with `byte2 & 0x80` as an extra half-step bit.
- Encoders map amplitude through a logarithmic table: amp ≈ 2^(enc/32)/8.7 for encoded values ≥ 0x20 [S2-rumble].

For one ERM or LRA motor on the real pad, decode each band's amplitude to a step `s` from 0 to 100 (high: `byte1 >> 1`; low: `(byte3 - 0x40) * 2 + (byte2 >> 7)`), map it back through the log table, and take `max(hi, lo)` per side. The table's shape [S2-rumble]: 0 is silent; 2^(1/4) per step up to s = 16 (≈0.115); 2^(1/16) per step up to s = 32 (≈0.23); 2^(1/32) per step up to s = 100 (1.0). Treat `00 01 40 40` as 0.

Reading the encoded value as linear is wrong: games mostly send s ≈ 40–70, which is 0.27–0.52 real strength but 0.4–0.7 linear, so every rumble feels much too strong. Max saw exactly that with v0.1.5 on Switch 2 (Mario Kart World, DualSense).

**UNCERTAIN.** Newer console firmware and some games send compact multi-sample rumble encodings. switch2-pico notes it decodes "packed multi-sample formats" [S16]. The simple decode above will mis-scale some of those frames. It is acceptable for on/off-level rumble.

Rumble is only meaningful after `48 01`. Switch 2 had vibration off by default for third-party Pro-compatible pads until the user enabled it in settings [S13].

---

## 4. Subcommands and the replies expected

Every reply is an input `0x21` (§3.3). ACK codes below are the ones genuine controllers send, per captures and dekuNukem [S2][S4].

| Sub | Console sends | ACK | Reply data (bytes 15+) |
|---|---|---|---|
| `02` | Request device info | `82` | Table 4.1 |
| `03` | Set input mode, 1 arg: `30` full, `31` NFC/IR, `3F` simple | `80` | none. After replying, start streaming 0x30, or 0x31 if asked; a 0x31 stream may carry zero MCU data. |
| `04` | Trigger elapsed time | `83` | 7 × uint16 LE in 10 ms units for L, R, ZL, ZR, SL, SR, HOME. Zeros are fine. Captures show non-zero values, e.g. `00 CC 00 EE 00 FF ...` [S4]. |
| `08` | Set shipment low-power state, arg `00` | `80` | none |
| `10` | SPI read: addr uint32 LE + len uint8 (max `0x1D`) | `90` | Echo the 4-byte address and the 1-byte length, then `len` bytes of data (§4.2) |
| `21` | Set MCU config (38-byte arg) | `A0` | 34 bytes: `01 00 FF 00 08 00 1B 01`, then zeros, with `C8` in the last byte (report byte 48). Real devices and both Linux emulators send this [S5][S6]. |
| `22` | Set MCU state: `00` suspend, `01` resume | `80` | none |
| `30` | Set player lights, 1 arg: low nibble = on, high nibble = flash | `80` | none. Record the player number: `0x1`, `0x3`, `0x7`, `0xF` = players 1–4 (cumulative pattern). Players 5–8 use other patterns. |
| `38` | Set HOME light (up to 25-byte pattern) | `80` | none. Optionally drive a LED. |
| `40` | IMU enable, `00` / `01` | `80` | none. Start or stop filling IMU bytes. |
| `41` | IMU sensitivity (4 bytes) | `80` | none |
| `48` | Vibration enable, `00` / `01` | `80` | none. nxbt sends `82`; captures show `80` [S4][S5]. |
| `06` | Set HCI state: `00` disconnect, `01`/`04` reboot-reconnect, `02` pair | `80` | none, then act on it |
| `00`, `05`, `07`, `31`, `43`, `50`, `52`… | rare | `80` | Generic: ACK with no data. Use `B0` + light byte for `31` and `D0` + uint16 voltage for `50` if wanted [S2]. |
| any unknown | | `80` | Answer **ACK `80` + the ID**. Don't NACK: nxbt found that a NACK makes the console retry forever [S5]. Log the ID, because new console firmware (including Switch 2) could add subcommands. |

### 4.1 Reply to `02` (12 data bytes)
| Off | Value | Meaning |
|---|---|---|
| 0–1 | FW version major, minor | Captures use `03 48` (Joy-Con 3.72) [S4][S7]. nxbt uses `03 8B` [S5]; joycontrol defaults to `04 00` [S6]. None of the sources shows the console checking this. Recommend `03 8B`, the value nxbt uses on firmware 12+ and with reported Switch 2 success. **UNCERTAIN** which values a genuine Pro sends today. |
| 2 | `03` | Type: 1 = Joy-Con L, 2 = Joy-Con R, **3 = Pro** |
| 3 | `02` | Unknown, always 2 |
| 4–9 | BD_ADDR **big-endian** | Must equal our real BT MAC |
| 10 | `01` | Unknown, always 1 |
| 11 | `01` | Use colours from SPI. Other values are believed to select default colours, or, on newer firmware, to signal "has grip colours" (**UNCERTAIN**). |

### 4.2 SPI reads the console makes, with suggested emulated contents
Recommended design: keep a small virtual SPI image covering 0x6000–0x60FF and 0x8000–0x803F. Serve **any** (address, length) request from it, so new reads don't break anything.

| Address, len | Region | Suggested content |
|---|---|---|
| `6000`, 0x10 | Serial number, ASCII | All `FF`. A first byte ≥ 0x80 means "no serial" [S3][S5]. |
| `6012` | Device type | `03` (Pro). Only matters if the image is read in bulk. |
| `601B` | Colour info present | `01` |
| `6020`, 0x18 | Factory IMU cal: accel origin xyz, accel coeff xyz, gyro origin xyz, gyro coeff xyz, all int16 LE | Origins `00 00` ×3; accel coeff `00 40` ×3 (0x4000); gyro origins `00 00` ×3; gyro coeff `3B 34` ×3 (0x343B). These are the default-sensitivity coefficients from [S3]. |
| `603D`, 0x19 | Left stick cal (9 bytes), right stick cal (9 bytes), `FF` spacer, body RGB, buttons RGB | Left order is Xmax-Δ, Ymax-Δ / Xc, Yc / Xmin-Δ, Ymin-Δ, each pair packed like §3.1: `00 06 60  00 08 80  00 06 60`. Right order is centre / min-Δ / max-Δ: `00 08 80  00 06 60  00 06 60`. Then `FF`, then e.g. body `32 32 32` and buttons `FF FF FF`. |
| `6050`, 0x0D (len varies) | Body RGB, buttons RGB, left grip RGB, right grip RGB (+1) | Same colours. Grips can repeat the body colour or be `FF`. |
| `6080`, 0x18 | 6-axis horizontal offsets (3 × int16 LE), then stick params 1 (18 bytes) | Pro offsets `50 FD 00 00 C6 0F` (−688, 0, 4038) [S3][S5]. Params: see below. |
| `6098`, 0x12 | Stick params 2 (18 bytes) | Same as params 1 |
| `8010`, 0x18 | User stick cal: magic `B2 A1` + 9 bytes, twice | All `FF` = no user calibration, so factory cal is used [S3][S5]. |
| `8026`, 0x1A | User IMU cal: magic + 24 bytes | All `FF` |

- **Stick calibration sanity rule.** For a Pro Controller the console checks the factory deltas from centre: Y-max, X-min and Y-min must each be ≥ `0x542`, and X-max ≥ `0x4D5`. Otherwise it silently applies a default calibration [S3]. The suggested Δ = 0x600 with centre 0x800 passes. Map the source gamepad so full deflection = centre ± 0x600 (0x200–0xE00).
- **Stick params.** The 18 bytes pack into 12 values using the same 3-byte pairing. Value 2 = radial dead zone; value 3 = range ratio [S3]. nxbt's Pro values decode to: dead zone `0x096`, ratio `0xF33`, others `0x4D4 0x541 0x541 0x541 0x9C7 0x9C7 0x633 0x633` [S5]. A smaller dead zone gives a more direct feel; the console applies this dead zone **on top of** ours.
- **IMU idle.** At the default ±8 g range, 1 g ≈ 4096 LSB. A pad lying flat reads roughly accel = (0, 0, +4096), gyro = 0 [S3]. Sending zeros with the IMU enabled is tolerated, but motion games will see free-fall.

---

## 5. Handshake sequence and the Change Grip/Order screen

Observed console-to-controller order on a successful first pair. The capture is of a Joy-Con, and a Pro follows the same pattern [S4]:
```
02 → 08 00 → 10 6000/10 → 10 603D/19 → 03 30 → 04 → 10 6080/18 → 10 6098/12
   → 10 8010/18 → 10 603D/19 → 10 6020/18 → 48 01 → 40 01 → 48 01 → 30 0x
```
Later, the console may also send `21`/`22` (MCU) and `38` (HOME LED) [S5][S6].

Behaviour notes:
- **Reply promptly, once per request.** Reply to each `0x01` with exactly one `0x21`. Interleave `0x30` reports only after `03 30`.
- **The console is slow on the Change Grip/Order screen.** Flooding it with reports during pairing causes failures.
  - joycontrol sleeps 300 ms after each subcommand reply, as an admitted hack [S6].
  - nxbt runs its pairing loop at ~15 Hz (66 ms). It treats pairing as done once both "player lights set" and "vibration enabled" have been seen, then switches to the fast loop [S5].
  - Recommendation: until `30 xx` has been received, send nothing unsolicited faster than about every 60 ms (0x21 replies are sent immediately). After that, stream 0x30.
- **Pairing is confirmed** when the player LED subcommand arrives. On the grip screen, the user then presses L+R (we have to forward those buttons) to confirm the grip.

---

## 6. Timing

| Item | Value | Source |
|---|---|---|
| 0x30 period: Joy-Con | ~15 ms (≈ 60–66 Hz) | [S1][S6] |
| 0x30 period: Pro Controller | ~8 ms (120 Hz) per dekuNukem | [S1] |
| Emulators | joycontrol 15 ms fixed; nxbt loop at 1/132 s but sends only on change, plus a ~1 s keep-alive | [S5][S6] |
| Firmware 12+ caveat | Too high a rate on some menus makes the console drop the link; nxbt added a variable rate | [S10] |
| IMU sample spacing | 3 samples per report, ~5 ms apart (designed around 15 ms) | [S3] |
| Supervision timeout (SDP) | 2.0 s | [S5] |

**Recommendation.** Start with 15 ms. Try 8 ms only after the handshake, and fall back to 15 ms if ACL TX buffers back up or the link drops. Always send a report at least every ~1 s even when nothing changed. Use `hci_number_of_completed_packets` / `l2cap_request_can_send_now_event` to pace, not a blind timer, so the queue never backs up. Added latency of 150–300 ms has been reported with slow BT adapters on firmware 12 [S10].

---

## 7. IMU packing

- **Layout.** Each sample is six int16 LE values: accel X/Y/Z, then gyro X/Y/Z. Three samples per 0x30 report (§3.2).
- **Default scale.**
  - Accel ±8 g: 0.000244 g/LSB, so 1 g ≈ 4096.
  - Gyro ±2000 dps: 0.061 dps/LSB [S3].
  - `40 01` resets to these defaults [S2].
- **Calibration.** The console applies the factory calibration from 0x6020; with the suggested values (origin 0, coefficients 0x4000 / 0x343B) raw values are used as-is.
- **Pro offsets.** The horizontal offset at 0x6080 (−688, 0, 4038) is the "flat" reference the console subtracts [S3].
- **Axes.** Axis orientation differs per device; the Pro uses the right Joy-Con style axes [S3 images]. **UNCERTAIN** for exact sign conventions on a Pro. Validate with the console's "Calibrate motion controls" screen.
- **Source pad without gyro.** Send a fixed gravity vector and zero gyro, or zeros.
- **YUCCA's choice** (`components/switch_pro`): with the IMU on, every sample equals the 0x6080 horizontal offsets (a real Pro's flat reading, which the console subtracts) and gyro 0. If the console's "Calibrate motion controls" screen disagrees on hardware, switch to (0, 0, 4096).

---

## 8. Quirks and risks

### 8.1 YUCCA-specific (BTstack + Bluepad32 on the original ESP32)
1. **PSM collision (certain).**
   - Bluepad32 calls `l2cap_register_service(..., PSM 0x13 / 0x11, ...)` in `uni_bt_bredr_setup()`. A second registration returns `L2CAP_SERVICE_ALREADY_REGISTERED` (`l2cap.c`).
   - So BTstack's `hid_device.c` can't be used as-is next to Bluepad32.
   - Options:
     - (a) Inside `uni_bt_packet_handler`, route `L2CAP_EVENT_INCOMING_CONNECTION` and data for the console's BD_ADDR to a Switch module. During pairing the remote is unknown, so decide by CoD: the console's CoD has major class Computer or Console. **UNCERTAIN** exactly what it reports, so capture it.
     - (b) Make outgoing connections only (reconnect path) using `l2cap_create_channel` with our own handler. This doesn't cover first pairing, because the console connects in.
   - Also add our own HID SDP record with `sdp_register_service`. Bluepad32 only registers a DID record today.
2. **Role, scatternet (unverified).**
   - The console is master of its piconet; real controllers are slaves.
   - Bluepad32 sets `hci_set_master_slave_policy(HCI_ROLE_MASTER)`, which asks for a role switch on every accepted incoming connection, including the console's.
     - If the console refuses, the link may still come up with us as slave.
     - If it accepts, we'd be master of the console. That is unusual and **untested**.
   - Use per-connection role logic: accept the console's link as slave.
   - The ESP32 BR/EDR controller must then run as master toward the gamepad and slave toward the console at the same time. ESP-IDF documents up to 7 ACL links. Whether scatternet with tight 8–15 ms periods works on the original ESP32 is **the first thing to prototype**.
3. **Sniff mode.**
   - Bluepad32 enables sniff and role switch in the default link policy.
   - If the console puts our link in sniff with a long interval, report latency rises. If the gamepad link is in sniff, the radio has more free time for the console link. **UNCERTAIN** what sniff parameters the console requests; log `HCI_EVENT_MODE_CHANGE`.
4. **Link key lifetime.**
   - Persist the console's link key in NVS (BTstack TLV) and never let gamepad-side "forget" logic delete it.
   - Store the console BD_ADDR separately, for the reconnect page.
5. **Licensing of the stack.** BTstack needs a commercial licence from BlueKitchen for commercial use (Bluepad32's LICENSE notes this). Bluepad32 itself is Apache-2.0.
6. **Coexistence.** Wi-Fi on the original ESP32 shares the radio. Keep Wi-Fi off while in Switch mode.

### 8.2 Known emulator issues (all platforms)
- **Pairing flood on the grip screen** (§5) [S6].
- **Firmware 12.0.0 (2021).**
  - Emulators stopped connecting.
  - nxbt's fix changed the CoD and controller emulation, and added a variable report rate.
  - Some users needed a previously known MAC.
  - Disconnects were mostly on Change Grip/Order [S10].
  - Lesson: future console updates can break us without warning. Keep values data-driven (name, CoD, FW version, rates) so they can be tuned in config.
- **Unreliable adapters.** Some BT adapters disconnect at random [S6]. On ESP32 the counterpart is controller scheduling under scatternet.
- **BlueZ input plugin.** It conflicts on Linux (not relevant to us).
- **NFC.** joycontrol removed NFC because it was unreliable (Switch MCU protocol) [S6]. Don't attempt amiibo.
- **Bluedroid precedent.** BlueCubeMod (ESP32, Bluedroid, Apache-2.0) shows an ESP32 can pass the handshake with canned 0x21 replies [S7]. It is a single-link design (no simultaneous gamepad host), so it says nothing about scatternet.

---

## 9. Switch 2

- **Official support.** Nintendo says the Switch 1 Pro Controller and Joy-Con "can be used by pairing wirelessly to a Nintendo Switch 2 console" [S12]. Pairing uses HOME > Controllers > Change Grip and Order and holding SYNC, the same as on Switch 1 [S9]. That means the Switch 2 keeps a BR/EDR HID host for Switch 1 controllers. Switch 2's own controllers (Joy-Con 2, Pro Controller 2) use BLE with a proprietary GATT protocol [S16], which is not relevant to us.
- **Wake from sleep.** A Switch 1 Pro Controller, genuine or 8BitDo, **cannot wake a Switch 2** [S14]. Switch 2 wakes on a BLE advertisement ("wake beacon"). Switch-bcon (Pico 2 W) records and replays that beacon and reports it verified on hardware [S15]. The original ESP32 can do BLE too, but this needs dual-mode operation and a captured beacon; treat it as optional and **UNCERTAIN**.
- **Third-party pads.** 8BitDo pads needed firmware updates before they worked on Switch 2. Older models were not supported at first [S13]. Nothing public says what changed. Possibly a stricter handshake or report-rate behaviour (**UNCERTAIN**).
- **Emulator reports** (as of 2025–26):
  - nxbt on Linux works "perfectly" for some users. Others can't connect or see "connecting" forever.
  - Fixes that helped: an auto-accepting SSP agent, `bluetoothctl trust`, and alias "Pro Controller".
  - A fork (NUXBT) patches pairing for Switch 2 [S11].
  - One gbatemp guide claims nxbt (typenoob's build) works on Switch 2, without detail [S17].
  - Switch-bcon reports Classic BT HID verified on hardware [S15].
  - Conclusion: **the Switch 1 protocol in this doc is accepted by Switch 2**, but pairing is more fragile. Test on both consoles early.
- **Pairing differences.** Observed 2026-10-17 (v0.1.0): the Switch 2 stays silent after opening the HID channels if the controller sends nothing (see §3.4). With v0.1.1 (0x3F hellos) the console sent `02` within 80 ms, then nothing until `08` 40 s later; YUCCA's hang-up took 25 s to complete too. The slave link to the console was starved while the DualSense link ran guaranteed QoS with a 1.25 ms poll, so the gamepad links are now polled at 10 ms (best effort) while the console is master. With v0.1.2 the full handshake (`02 08 10 10 03 04 10… 40 30 48 21 30`) finished in under 1 s, with a DualSense and with an 8BitDo SN30 Pro. YUCCA then hung up to re-page as master (the Switch 1 hand-off), and the Switch 2 refused every page with 0x13. So for Switch 2, YUCCA stays slave on the link the console opened, and its own pages allow the console to switch roles (`Profile::console_master`). With v0.1.3 pairing and play worked; after a reboot, YUCCA's page was accepted and the console took the master role, then dropped the link with 0x05 (authentication failure). BTstack stores a link key only if one side asked for bonding during the IO capability exchange, so YUCCA now stores the console's key itself on HCI Link Key Notification and logs whether it has a key when the console asks. With v0.1.4 (key stored at once) one pairing link died with 0x08 two seconds after the flash write and no subcommand arrived, so the write now waits 5 s or until the link drops. The reconnect still failed with 0x05, and no link key request ever reached YUCCA: the Switch 2 waits for the pager to authenticate, so with `console_master` YUCCA opens its channels at security level 2 (authenticate and encrypt with the stored key). Pages from YUCCA after the earlier failed pairings were accepted at baseband and dropped with reason 0x13 within 1 s, before any L2CAP channel. With v0.1.5 the reconnect worked: the console asked for the key, authentication succeeded, YUCCA stayed slave. But the pairing link completed its handshake at 19.7 s, the key was written at 23.7 s, and the link died with 0x08 at 35.9 s; v0.1.3, which wrote no key, kept its pairing link. So the key is now written only when the console link is down (a power cut while connected loses it; re-pair then). After the reconnect the console sent `02`, then nothing for 15 s before `08` and the rest of the handshake; cause unknown. Play dropped and reconnected repeatedly while Bluepad32 logged hundreds of "circular buffer full" (output reports to the DualSense backing up), and rumble felt far too strong (linear decode of the log-scale amplitude, §3.5). Rumble strength changes now reach the pad at most every 100 ms. With v0.1.6 the reconnect handshake ran without the stall and no console link drop was logged, but Max still saw drops in play, the "circular buffer full" flood came back, and rumble still felt strong. Starting and stopping were exempt from the 100 ms limit, and games pulse rumble on and off every frame, so now every rumble change waits for the limit (tick sends the latest; link down clears at once). A "Rumble strength (%)" setting (default 60) scales all rumble. Expect the same subcommand handshake. Log any unknown subcommand IDs a Switch 2 sends.
- **Rumble.** It was off by default for third-party Pro-compatible pads on Switch 2 until enabled in settings [S13].

---

## 10. Open-source implementations and licences

| Project | What | Licence | Use for YUCCA |
|---|---|---|---|
| dekuNukem/Nintendo_Switch_Reverse_Engineering [S1][S2][S3] | Protocol RE notes (HID, subcommands, SPI, IMU, rumble) | **No licence file** (GitHub API reports none; all rights reserved by default) | Facts only. Describe in our own words; don't copy text or tables verbatim. |
| mart1nro/joycontrol [S6]; Poohl/joycontrol fork | Python/BlueZ emulator (Pro, JC-L, JC-R) | **GPL-3.0** | Read for behaviour only; **do not copy code** |
| Brikwerk/nxbt [S5] | Python/BlueZ emulator, macros, Switch 1 fw 12+ fixes | **MIT** | Safe to reuse values (SDP record, descriptor, SPI contents) with attribution |
| NathanReeves/BlueCubeMod [S7] | ESP32 (Bluedroid) GameCube → Switch over BT | **Apache-2.0** | Compatible with MIT if NOTICE/attribution is kept. Useful ESP32 precedent. |
| darthcloud/BlueRetro | ESP32 BT **host** adapter to retro consoles; parses Switch pads as host | **Apache-2.0** | Not an emulator of Switch output. Relevant only for the gamepad-in side. |
| Moi-poke/Switch-bcon [S15] | Pico 2 W (BTstack) Pro/Joy-Con emulator, USB + Classic BT, Switch 2 BLE wake | **PolyForm Noncommercial 1.0.0** | Do not copy. Read for behaviour only (closest analogue: BTstack + Switch 2). |
| Wilstride/PicoSwitchController | Pico W (BTstack) Pro Controller over BT | **GPL-3.0** | Don't copy |
| OpenStickCommunity/GP2040-CE PR #1575 | Pico W Switch Bluetooth mode (BTstack), reconnect after restart | GP2040-CE is **MIT** (merge status of the PR not confirmed) | Possibly reusable BTstack reference. Check merge state and the code's origin. The PR author says it was largely AI-written. |
| HandHeldLegend/hoja-lib-esp32 | ESP32 Switch Pro BT emulation (HOJA) | **CC BY-NC-SA 4.0** | Do not copy (non-commercial, share-alike) |
| espp `switch_pro` component | ESP-IDF Pro Controller protocol engine, **USB only** (S3/S2/P4) | **MIT** | Handshake logic reference. It doesn't do BT. |
| joshbirnholz/switch2-pico [S16] | Switch 2 controllers (BLE) → USB Pro Controller dongle | **MIT** | Reference for rumble decoding of packed formats; opposite direction |
| SDL `SDL_hidapi_switch.c` [S8] | Host-side Pro/Joy-Con driver | **zlib** | Permissive, cross-check for report parsing and SPI addresses |
| Linux `hid-nintendo` | Host-side kernel driver | GPL-2.0 | Read only |
| timmeh87/switchnotes [S4] | Raw capture of a console pairing session | No licence stated | Facts (byte sequences) only |
| BTstack (bluekitchen) | Our host stack; `hid_device.c` has an SDP helper | BlueKitchen licence (non-commercial free, commercial needs licence) | Already in tree via Bluepad32 (Apache-2.0 wrapper) |

---

## 11. Implementation checklist (derived)
1. Spike first: ESP32 connected to one gamepad (master) plus a console link (slave), streaming 0x30 at 15 ms. Measure latency and drops before investing further.
2. Add a Switch HID SDP record (§2.2/§2.3). Set name and CoD while in Switch mode. Set IO capability NoInputNoOutput.
3. Add a demux in Bluepad32's L2CAP handler: the console's BD_ADDR goes to the Switch module, everything else to Bluepad32.
4. Subcommand state machine (§4), virtual SPI image (§4.2), 0x30 streamer with 12-bit stick mapping (centre 0x800 ± 0x600).
5. Pairing mode: discoverable + connectable while the user triggers "pair to Switch". Store the console BD_ADDR and link key.
6. Reconnect: page the console's BD_ADDR, then open 0x11 → 0x13. Retry with backoff; never auto-delete the key.
7. Rumble: decode §3.5 into the source pad's rumble API.

---

## 12. In YUCCA

- `components/switch_pro`: the protocol (replies, 0x30/0x31, SPI image, rumble). `components/bt_hid_device`: SDP, PSMs, pairing, the stored console, paging. `components/target_switch`: glue and reconnect policy. One target serves both consoles (`target=switch` / `switch2`), each with its own stored console.
- Pairing runs with the console as master (it pages us). 2 s after the console sets the player lights (time for L + R on the grip screen), or after 15 s without lights, YUCCA hangs up and pages it as master from 1 s later, retrying for 60 s, so the ESP32 is master of both links (as on the Wii). That the console accepts this right after pairing is **UNVERIFIED**; `docs/hw-tests/switch.md` step 3 checks it. **Switch 2 differs:** no hand-off; YUCCA stays the console's slave, gamepads are polled at 10 ms while it is, and pages let the console take the master role.
- Reports: 15 ms; before the player lights, at most one full report per 60 ms (section 5). Subcommand replies go out at once, oldest first.
- `06 00` (console sleeping): ACK (hang-up waits up to 200 ms for it to leave), hang up, no auto-reconnect; a button press pages the console again. Any other drop: page again for 60 s (15 s after reason 0x15).
- Not in v1: motion (IMU reports idle samples), NFC/IR (0x31 carries zeros), Switch 2 wake beacon.

## Sources
- [S1] dekuNukem et al., *bluetooth_hid_notes.md*: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_notes.md (no licence)
- [S2] *bluetooth_hid_subcommands_notes.md*: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md
- [S2-rumble] *rumble_data_table.md*: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md
- [S3] *spi_flash_notes.md* and *imu_sensor_notes.md*: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/spi_flash_notes.md, https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/imu_sensor_notes.md
- [S4] timmeh87, *console_pairing_session*: https://github.com/timmeh87/switchnotes/blob/master/console_pairing_session
- [S5] Brikwerk/nxbt (MIT): `nxbt/controller/sdp/switch-controller.xml`, `protocol.py`, `server.py`: https://github.com/Brikwerk/nxbt
- [S6] mart1nro/joycontrol (GPL-3.0), README and `joycontrol/protocol.py`: https://github.com/mart1nro/joycontrol
- [S7] NathanReeves/BlueCubeMod (Apache-2.0), `Firmware/BlueCubeModv2/main/main.c`: https://github.com/NathanReeves/BlueCubeMod
- [S8] SDL, `src/joystick/hidapi/SDL_hidapi_switch.c` (zlib): https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c
- [S9] Nintendo Support, *How to pair the Nintendo Switch Pro Controller* (Switch 2 steps): https://en-americas-support.nintendo.com/app/answers/detail/a_id/22417/p/1095/c/715
- [S10] nxbt issue #18, *Support for SwitchOS 12*: https://github.com/Brikwerk/nxbt/issues/18
- [S11] nxbt issue #186, *Discussion about Switch 2*: https://github.com/Brikwerk/nxbt/issues/186; also Discussion #177: https://github.com/Brikwerk/nxbt/discussions/177
- [S12] Nintendo AU, *Compatibility of Nintendo Switch and Nintendo Switch 2*: https://www.nintendo.com/au/support/articles/compatibility-of-nintendo-switch-and-nintendo-switch-2
- [S13] Lon.TV, *8BitDo Controller Compatibility with the Switch 2* (2025-06-22): https://blog.lon.tv/2025/06/22/8bitdo-controller-compatibility-with-the-switch-2/
- [S14] Lon.TV, *Can an 8bitdo Controller Wake Up a Switch 2?*: https://blog.lon.tv/?p=4160
- [S15] Moi-poke/Switch-bcon (PolyForm NC): https://github.com/Moi-poke/Switch-bcon
- [S16] joshbirnholz/switch2-pico (MIT): https://github.com/joshbirnholz/switch2-pico
- [S17] gbatemp, *How to install and use NXBT (Switch 1 and Switch 2 supported)*: https://gbatemp.net/threads/how-to-install-and-use-nxbt-switch-1-and-switch-2-supported.676535/
- Also: GP2040-CE PR #1575 https://github.com/OpenStickCommunity/GP2040-CE/pull/1575 ; Wilstride/PicoSwitchController https://github.com/Wilstride/PicoSwitchController ; espp switch_pro https://components.espressif.com/components/espp/switch_pro ; HandHeldLegend/hoja-lib-esp32 https://github.com/HandHeldLegend/hoja-lib-esp32 ; darthcloud/BlueRetro https://github.com/darthcloud/BlueRetro
- Local code checked: `/home/claude/yucca/external/bluepad32/src/components/bluepad32/bt/uni_bt_bredr.c` (PSM registration, role policy, SSP auto-accept), `.../bt/uni_bt_sdp.c` (DID record), `.../external/btstack/src/classic/hid_device.{c,h}`, `.../external/btstack/src/l2cap.c` (`L2CAP_SERVICE_ALREADY_REGISTERED`).
