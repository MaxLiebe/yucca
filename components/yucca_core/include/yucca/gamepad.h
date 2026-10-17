// Canonical controller state shared by every input driver and target.
// No ESP-IDF or Bluetooth headers here: this is compiled by host tests too.
#pragma once

#include <cstdint>

namespace yucca {

// Buttons by position. Face buttons follow the compass, so a DualSense Cross,
// an Xbox A and a Nintendo B are all South.
enum class Button : uint8_t {
    South,
    East,
    West,
    North,
    L1,
    R1,
    L2,  // digital click of the left trigger (or a digital-only trigger)
    R2,
    L3,
    R3,
    Select,  // Share / Create / View / Minus
    Start,   // Options / Menu / Plus
    Home,    // PS / Guide / Home
    Capture,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

constexpr uint32_t bit(Button b) { return 1u << static_cast<uint8_t>(b); }

struct GamepadState {
    uint32_t buttons = 0;
    // Sticks -1..1, +x right, +y up. Deadzone already applied by the input.
    float lx = 0, ly = 0, rx = 0, ry = 0;
    // Analog triggers 0..1. A digital-only trigger reports 0 or 1.
    float l2 = 0, r2 = 0;
    // Motion, when the controller has it. Gyro in deg/s, accel in g.
    bool has_motion = false;
    float gyro[3] = {0, 0, 0};
    float accel[3] = {0, 0, 0};
    // Battery 0..100 %, or -1 when unknown.
    int8_t battery = -1;

    bool pressed(Button b) const { return (buttons & bit(b)) != 0; }
    void set(Button b, bool on)
    {
        if (on) {
            buttons |= bit(b);
        } else {
            buttons &= ~bit(b);
        }
    }
    bool any_input() const { return buttons != 0; }
};

// What a console asks the controller to show or do.
struct Feedback {
    float rumble_left = 0;   // 0..1, heavy motor
    float rumble_right = 0;  // 0..1, light motor
    uint8_t player_leds = 0; // bit 0 = player 1 LED
};

// Radial deadzone, rescaled so the edge still reaches full deflection.
void apply_radial_deadzone(float &x, float &y, float deadzone);

}  // namespace yucca
