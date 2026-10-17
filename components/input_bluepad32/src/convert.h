// Bluepad32 gamepad report -> GamepadState, free of Bluepad32 headers so host
// tests can run it. Bit values match uni_gamepad.h (checked in bluepad32_input.cpp).
#pragma once

#include <cstdint>

#include "yucca/gamepad.h"

namespace yucca::bp32 {

constexpr uint16_t kButtonA = 1 << 0;  // south
constexpr uint16_t kButtonB = 1 << 1;  // east
constexpr uint16_t kButtonX = 1 << 2;  // west
constexpr uint16_t kButtonY = 1 << 3;  // north
constexpr uint16_t kShoulderL = 1 << 4;
constexpr uint16_t kShoulderR = 1 << 5;
constexpr uint16_t kTriggerL = 1 << 6;
constexpr uint16_t kTriggerR = 1 << 7;
constexpr uint16_t kThumbL = 1 << 8;
constexpr uint16_t kThumbR = 1 << 9;
constexpr uint8_t kMiscSystem = 1 << 0;
constexpr uint8_t kMiscSelect = 1 << 1;
constexpr uint8_t kMiscStart = 1 << 2;
constexpr uint8_t kMiscCapture = 1 << 3;
constexpr uint8_t kDpadUp = 1 << 0;
constexpr uint8_t kDpadDown = 1 << 1;
constexpr uint8_t kDpadRight = 1 << 2;
constexpr uint8_t kDpadLeft = 1 << 3;

struct RawPad {
    uint8_t dpad = 0;
    int32_t axis_x = 0, axis_y = 0, axis_rx = 0, axis_ry = 0;  // -512..511, +y down
    int32_t brake = 0, throttle = 0;                           // 0..1023
    uint16_t buttons = 0;
    uint8_t misc_buttons = 0;
    float gyro[3] = {0, 0, 0};   // rad/s
    float accel[3] = {0, 0, 0};  // m/s^2
    uint8_t battery = 0;         // 0 unknown, 1 empty .. 255 full
};

constexpr float kStickDeadzone = 0.08f;

GamepadState convert(const RawPad &raw);

}  // namespace yucca::bp32
