#include "convert.h"

namespace yucca::bp32 {

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

GamepadState convert(const RawPad &raw)
{
    GamepadState s;
    s.set(Button::South, raw.buttons & kButtonA);
    s.set(Button::East, raw.buttons & kButtonB);
    s.set(Button::West, raw.buttons & kButtonX);
    s.set(Button::North, raw.buttons & kButtonY);
    s.set(Button::L1, raw.buttons & kShoulderL);
    s.set(Button::R1, raw.buttons & kShoulderR);
    s.set(Button::L2, raw.buttons & kTriggerL);
    s.set(Button::R2, raw.buttons & kTriggerR);
    s.set(Button::L3, raw.buttons & kThumbL);
    s.set(Button::R3, raw.buttons & kThumbR);
    s.set(Button::Select, raw.misc_buttons & kMiscSelect);
    s.set(Button::Start, raw.misc_buttons & kMiscStart);
    s.set(Button::Home, raw.misc_buttons & kMiscSystem);
    s.set(Button::Capture, raw.misc_buttons & kMiscCapture);
    s.set(Button::DpadUp, raw.dpad & kDpadUp);
    s.set(Button::DpadDown, raw.dpad & kDpadDown);
    s.set(Button::DpadLeft, raw.dpad & kDpadLeft);
    s.set(Button::DpadRight, raw.dpad & kDpadRight);

    s.lx = raw.axis_x / 512.0f;
    s.ly = -raw.axis_y / 512.0f;
    s.rx = raw.axis_rx / 512.0f;
    s.ry = -raw.axis_ry / 512.0f;
    apply_radial_deadzone(s.lx, s.ly, kStickDeadzone);
    apply_radial_deadzone(s.rx, s.ry, kStickDeadzone);

    // Digital-only triggers (SN30 Pro, Switch Pro) only set the button.
    s.l2 = (raw.buttons & kTriggerL) ? 1.0f : clamp01(raw.brake / 1023.0f);
    s.r2 = (raw.buttons & kTriggerR) ? 1.0f : clamp01(raw.throttle / 1023.0f);

    constexpr float kRadToDeg = 57.29577951f;
    constexpr float kGravity = 9.80665f;
    for (int i = 0; i < 3; i++) {
        s.gyro[i] = raw.gyro[i] * kRadToDeg;
        s.accel[i] = raw.accel[i] / kGravity;
        if (raw.gyro[i] != 0 || raw.accel[i] != 0) {
            s.has_motion = true;
        }
    }

    // 1 is empty; anything above reads at least 1 % so only empty warns.
    if (raw.battery == 0) {
        s.battery = -1;
    } else if (raw.battery == 1) {
        s.battery = 0;
    } else {
        const int pct = ((raw.battery - 1) * 100 + 127) / 254;
        s.battery = static_cast<int8_t>(pct < 1 ? 1 : pct);
    }
    return s;
}

}  // namespace yucca::bp32
