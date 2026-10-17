// Switch Pro Controller protocol over Bluetooth HID, without the Bluetooth:
// subcommand replies (0x21), the full input report (0x30), rumble decoding.
// Facts and sources: docs/platforms/switch.md. Host-tested; no ESP-IDF headers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yucca::switchpro {

// Button bits as they appear in input report bytes 3 (right), 4 (shared) and
// 5 (left): buttons = byte3 | byte4 << 8 | byte5 << 16.
enum : uint32_t {
    kY = 1u << 0,
    kX = 1u << 1,
    kB = 1u << 2,
    kA = 1u << 3,
    kR = 1u << 6,
    kZR = 1u << 7,
    kMinus = 1u << 8,
    kPlus = 1u << 9,
    kRStick = 1u << 10,
    kLStick = 1u << 11,
    kHome = 1u << 12,
    kCapture = 1u << 13,
    kDown = 1u << 16,
    kUp = 1u << 17,
    kRight = 1u << 18,
    kLeft = 1u << 19,
    kL = 1u << 22,
    kZL = 1u << 23,
};

// Sticks are 12-bit, +Y up. The emulated factory calibration says full
// deflection is center +-kStickRange.
constexpr uint16_t kStickCenter = 0x800;
constexpr uint16_t kStickRange = 0x600;

// Input report and 0x21/0x30 payload size (report id included, no 0xA1).
constexpr size_t kReportSize = 49;
// The 0x31 report (NFC/IR mode): the 0x30 bytes plus MCU data, here zeros.
constexpr size_t kReportSizeMcu = 362;

struct State {
    uint32_t buttons = 0;
    uint16_t lx = kStickCenter, ly = kStickCenter, rx = kStickCenter, ry = kStickCenter;
    uint8_t battery = 8;  // 0 empty, 2 critical, 4 low, 6 medium, 8 full
    bool charging = false;
};

// Per-side rumble strength 0..1 from one 4-byte HD rumble group.
float decode_rumble(const uint8_t group[4]);

// Map -1..1 onto the calibrated 12-bit range.
uint16_t stick_from_unit(float v);

class Protocol {
public:
    // addr: our Bluetooth address as printed (most significant byte first).
    explicit Protocol(const uint8_t addr[6]);

    // Forget per-connection state (mode, IMU, vibration, lights). Call when
    // the console link comes up.
    void reset();

    void set_state(const State &s) { state_ = s; }
    const State &state() const { return state_; }

    // A report from the console (output 0x01 / 0x10, report id first, no
    // 0xA2). If it carries a subcommand, the 0x21 reply is written to `reply`
    // (kReportSize bytes) and true is returned: send it right away.
    bool handle_output(const uint8_t *data, size_t len, uint8_t *reply);

    // The periodic input report (0x30, or 0x31 when the console asked for
    // NFC/IR mode) once the console asked for full mode. `out` holds
    // kReportSizeMcu bytes. Returns the length, or 0 while none is due.
    size_t build_input(uint8_t *out);

    bool full_mode() const { return mode_ == 0x30 || mode_ == 0x31; }
    uint8_t input_mode() const { return mode_; }
    // Player lights set: the console finished its handshake.
    bool paired() const { return lights_set_; }
    uint8_t player_leds() const { return leds_; }  // bit 0 = LED 1 (flashing ones too)
    bool vibration_enabled() const { return vibration_; }
    float rumble_left() const { return vibration_ ? rumble_left_ : 0.f; }
    float rumble_right() const { return vibration_ ? rumble_right_ : 0.f; }
    // Subcommand 06 00: the console asks us to disconnect.
    bool disconnect_requested() const { return disconnect_; }
    // Last subcommand id answered (for logs).
    uint8_t last_subcommand() const { return last_sub_; }

    // The emulated SPI flash (exposed for tests).
    static uint8_t spi_byte(uint32_t addr);

private:
    void header(uint8_t *out, uint8_t id);
    void reply_to(uint8_t sub, const uint8_t *args, size_t args_len, uint8_t *out);

    uint8_t addr_[6];
    State state_;
    uint8_t timer_ = 0;
    uint8_t mode_ = 0x3f;  // simple HID mode until the console sets 0x30
    bool imu_ = false;
    bool vibration_ = false;
    bool lights_set_ = false;
    bool disconnect_ = false;
    uint8_t leds_ = 0;
    uint8_t last_sub_ = 0;
    float rumble_left_ = 0.f;
    float rumble_right_ = 0.f;
};

}  // namespace yucca::switchpro
