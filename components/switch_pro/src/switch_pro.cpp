#include "yucca/switch_pro.h"

#include <cmath>
#include <cstring>

namespace yucca::switchpro {

// --- Emulated SPI flash (docs/platforms/switch.md 4.2). Unset flash reads 0xFF.

// Two 12-bit values packed into 3 bytes, as sticks and calibration use them.
#define PAIR(x, y) uint8_t((x) & 0xff), uint8_t((((x) >> 8) & 0x0f) | (((y) & 0x0f) << 4)), uint8_t((y) >> 4)

// 0x6020: accel origin, accel coefficient, gyro origin, gyro coefficient (int16 LE x3 each).
static const uint8_t kImuCal[0x18] = {
    0, 0, 0, 0, 0, 0, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
    0, 0, 0, 0, 0, 0, 0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34,
};
// 0x603D: left stick (max delta, center, min delta), right stick (center,
// min delta, max delta), spacer, body and button colours.
static const uint8_t kStickCal[0x19] = {
    PAIR(kStickRange, kStickRange), PAIR(kStickCenter, kStickCenter), PAIR(kStickRange, kStickRange),
    PAIR(kStickCenter, kStickCenter), PAIR(kStickRange, kStickRange), PAIR(kStickRange, kStickRange),
    0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff,
};
// 0x6050: body, buttons, left grip, right grip colours.
static const uint8_t kColours[0x0d] = {0x32, 0x32, 0x32, 0xff, 0xff, 0xff, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0xff};
// 0x6080: horizontal IMU offsets of a Pro (-688, 0, 4038), then stick
// parameters; 0x6098: the same parameters again (dead zone 0x096, range
// ratio 0xF33; values from nxbt, MIT).
static const int16_t kFlat[3] = {-688, 0, 4038};
static const uint8_t kStickParams[0x12] = {
    PAIR(0x00f, 0x613), PAIR(0x096, 0xf33), PAIR(0x4d4, 0x541), PAIR(0x541, 0x541), PAIR(0x9c7, 0x9c7), PAIR(0x633, 0x633),
};

#undef PAIR

uint8_t Protocol::spi_byte(uint32_t addr)
{
    auto in = [addr](uint32_t base, size_t len) { return addr >= base && addr < base + len; };
    if (addr == 0x6012) {
        return 0x03;  // Pro Controller
    }
    if (addr == 0x601b) {
        return 0x01;  // colours present
    }
    if (in(0x6020, sizeof(kImuCal))) {
        return kImuCal[addr - 0x6020];
    }
    if (in(0x603d, sizeof(kStickCal))) {
        return kStickCal[addr - 0x603d];
    }
    if (in(0x6050, sizeof(kColours))) {
        return kColours[addr - 0x6050];
    }
    if (in(0x6080, 6)) {
        const auto v = static_cast<uint16_t>(kFlat[(addr - 0x6080) / 2]);
        return (addr - 0x6080) % 2 ? uint8_t(v >> 8) : uint8_t(v);
    }
    if (in(0x6086, sizeof(kStickParams))) {
        return kStickParams[addr - 0x6086];
    }
    if (in(0x6098, sizeof(kStickParams))) {
        return kStickParams[addr - 0x6098];
    }
    return 0xff;  // serial number, user calibration (0x8010..): none
}

// --- Rumble

// Amplitudes are sent as a 0..100 step on a log scale (docs/platforms/switch.md
// §3.5): 2^(1/4) per step up to ~0.115, 2^(1/16) up to ~0.23, 2^(1/32) up to 1.0.
static float amp_from_step(int s)
{
    if (s <= 0) return 0.f;
    if (s >= 100) return 1.f;
    if (s >= 32) return std::exp2((s - 100) / 32.f);
    const float at32 = std::exp2(-68 / 32.f);
    if (s >= 16) return at32 * std::exp2((s - 32) / 16.f);
    return at32 * std::exp2(-1.f) * std::exp2((s - 16) / 4.f);
}

float decode_rumble(const uint8_t g[4])
{
    if (g[0] == 0x00 && g[1] == 0x01 && g[2] == 0x40 && g[3] == 0x40) {
        return 0.f;
    }
    const float hi = amp_from_step(g[1] >> 1);
    const float lo = amp_from_step((int(g[3]) - 0x40) * 2 + (g[2] >> 7));
    return hi > lo ? hi : lo;
}

uint16_t stick_from_unit(float v)
{
    if (!(v >= -1.f)) v = -1.f;  // also NaN
    if (v > 1.f) v = 1.f;
    // Positive raw values: +0.5 then truncation rounds to nearest.
    return static_cast<uint16_t>(kStickCenter + v * kStickRange + 0.5f);
}

// --- Protocol

Protocol::Protocol(const uint8_t addr[6]) { std::memcpy(addr_, addr, 6); }

void Protocol::reset()
{
    mode_ = 0x3f;
    imu_ = false;
    vibration_ = false;
    lights_set_ = false;
    disconnect_ = false;
    heard_ = false;
    leds_ = 0;
    rumble_left_ = rumble_right_ = 0.f;
}

static void put_stick(uint8_t *p, uint16_t x, uint16_t y)
{
    p[0] = uint8_t(x);
    p[1] = uint8_t(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    p[2] = uint8_t(y >> 4);
}

void Protocol::header(uint8_t *out, uint8_t id)
{
    std::memset(out, 0, kReportSize);
    out[0] = id;
    out[1] = timer_++;
    const uint8_t level = (state_.battery > 8 ? 8 : state_.battery) & 0x0e;  // even levels; bit 0 is "charging"
    out[2] = uint8_t((level << 4) | (state_.charging ? 0x10 : 0));  // low nibble 0: Pro, on battery
    out[3] = uint8_t(state_.buttons);
    out[4] = uint8_t(state_.buttons >> 8);
    out[5] = uint8_t(state_.buttons >> 16);
    put_stick(out + 6, state_.lx, state_.ly);
    put_stick(out + 9, state_.rx, state_.ry);
    out[12] = 0x80;  // "vibrator input report": constant in every capture
}

size_t Protocol::build_hello(uint8_t *out)
{
    if (heard_) {
        return 0;
    }
    std::memset(out, 0, kHelloSize);
    out[0] = 0x3f;
    out[3] = 0x08;  // hat neutral
    for (int i = 0; i < 4; i++) {
        out[5 + i * 2] = 0x80;  // axes centred: 0x8000
    }
    return kHelloSize;
}

size_t Protocol::build_input(uint8_t *out)
{
    if (!full_mode()) {
        return 0;
    }
    header(out, mode_);
    if (imu_) {
        // A controller lying still and flat: three identical samples equal to
        // the horizontal offsets in SPI 0x6080, which is what a real Pro
        // reads when flat (UNCERTAIN, see docs/platforms/switch.md 7).
        for (int s = 0; s < 3; s++) {
            uint8_t *p = out + 13 + s * 12;
            for (int i = 0; i < 3; i++) {
                p[i * 2] = uint8_t(kFlat[i]);
                p[i * 2 + 1] = uint8_t(uint16_t(kFlat[i]) >> 8);
            }
        }
    }
    if (mode_ == 0x31) {
        std::memset(out + kReportSize, 0, kReportSizeMcu - kReportSize);  // no NFC/IR data
        return kReportSizeMcu;
    }
    return kReportSize;
}

void Protocol::reply_to(uint8_t sub, const uint8_t *args, size_t n, uint8_t *out)
{
    header(out, 0x21);
    uint8_t *data = out + 15;
    uint8_t ack = 0x80;
    out[14] = sub;
    switch (sub) {
    case 0x02: {  // device info
        ack = 0x82;
        // Firmware 4.33, the current Pro Controller release: since 2026-10-18
        // a Switch 2 drops a controller reporting 3.139 after its setup reads.
        const uint8_t info[12] = {0x04, 0x21, 0x03, 0x02, addr_[0], addr_[1], addr_[2],
                                  addr_[3], addr_[4], addr_[5], 0x01, 0x01};
        std::memcpy(data, info, sizeof(info));
        break;
    }
    case 0x03:  // input mode
        if (n >= 1 && (args[0] == 0x30 || args[0] == 0x31 || args[0] == 0x3f)) {
            mode_ = args[0];
        }
        break;
    case 0x04:  // trigger elapsed time: zeros
        ack = 0x83;
        break;
    case 0x06:  // HCI state: 00 = disconnect
        if (n >= 1 && args[0] == 0x00) {
            disconnect_ = true;
        }
        break;
    case 0x10: {  // SPI read
        ack = 0x90;
        if (n >= 5) {
            const uint32_t addr = args[0] | args[1] << 8 | args[2] << 16 | uint32_t(args[3]) << 24;
            uint8_t len = args[4] > 0x1d ? 0x1d : args[4];
            std::memcpy(data, args, 4);
            data[4] = len;
            for (uint8_t i = 0; i < len; i++) {
                data[5 + i] = spi_byte(addr + i);
            }
        }
        break;
    }
    case 0x21: {  // MCU config
        ack = 0xa0;
        const uint8_t mcu[8] = {0x01, 0x00, 0xff, 0x00, 0x08, 0x00, 0x1b, 0x01};
        std::memcpy(data, mcu, sizeof(mcu));
        out[48] = 0xc8;
        break;
    }
    case 0x30:  // player lights: low nibble on, high nibble flashing
        if (n >= 1) {
            leds_ = uint8_t((args[0] | (args[0] >> 4)) & 0x0f);
            lights_set_ = true;
        }
        break;
    case 0x40:
        imu_ = n >= 1 && args[0] != 0;
        break;
    case 0x48:
        vibration_ = n >= 1 && args[0] != 0;
        break;
    default:
        // Everything else (08 shipment, 22 MCU state, 38 HOME light, 41 IMU
        // sensitivity, unknown ids): a plain ACK. A NACK makes the console
        // retry forever.
        break;
    }
    out[13] = ack;
    last_sub_ = sub;
}

bool Protocol::handle_output(const uint8_t *data, size_t len, uint8_t *reply)
{
    if (len < 10 || (data[0] != 0x01 && data[0] != 0x10)) {
        return false;  // 0x11/0x12 (MCU) and short packets: ignore
    }
    heard_ = true;
    rumble_left_ = decode_rumble(data + 2);
    rumble_right_ = decode_rumble(data + 6);
    if (data[0] != 0x01 || len < 11) {
        return false;
    }
    reply_to(data[10], data + 11, len - 11, reply);
    return true;
}

}  // namespace yucca::switchpro
