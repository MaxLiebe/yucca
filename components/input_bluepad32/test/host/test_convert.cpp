#include "../../../yucca_core/test/host/check.h"
#include "convert.h"

using namespace yucca;
using namespace yucca::bp32;

static void test_buttons_by_position()
{
    RawPad raw;
    raw.buttons = kButtonA | kShoulderR | kThumbL;
    raw.misc_buttons = kMiscSystem | kMiscSelect;
    raw.dpad = kDpadUp | kDpadLeft;
    GamepadState s = convert(raw);
    CHECK(s.pressed(Button::South));
    CHECK(!s.pressed(Button::East));
    CHECK(s.pressed(Button::R1));
    CHECK(s.pressed(Button::L3));
    CHECK(s.pressed(Button::Home));
    CHECK(s.pressed(Button::Select));
    CHECK(!s.pressed(Button::Start));
    CHECK(s.pressed(Button::DpadUp));
    CHECK(s.pressed(Button::DpadLeft));
    CHECK(!s.pressed(Button::DpadRight));
}

static void test_sticks()
{
    RawPad raw;
    raw.axis_x = 511;
    raw.axis_rx = 20;  // inside the deadzone
    GamepadState s = convert(raw);
    CHECK(s.lx > 0.99f);
    CHECK_NEAR(s.ly, 0.0);
    CHECK_NEAR(s.rx, 0.0);

    raw = {};
    raw.axis_ry = -512;  // Bluepad32 up is negative
    s = convert(raw);
    CHECK(s.ry > 0.99f);  // YUCCA up is positive
}

static void test_triggers()
{
    RawPad raw;
    raw.brake = 1023;
    raw.throttle = 512;
    GamepadState s = convert(raw);
    CHECK_NEAR(s.l2, 1.0);
    CHECK(s.r2 > 0.49f && s.r2 < 0.51f);
    CHECK(!s.pressed(Button::L2));

    // Digital-only trigger: the button sets full travel.
    raw = {};
    raw.buttons = kTriggerR;
    s = convert(raw);
    CHECK_NEAR(s.r2, 1.0);
    CHECK(s.pressed(Button::R2));
}

static void test_battery_and_motion()
{
    RawPad raw;
    CHECK(convert(raw).battery == -1);
    CHECK(!convert(raw).has_motion);
    raw.battery = 1;
    CHECK(convert(raw).battery == 0);
    raw.battery = 2;
    CHECK(convert(raw).battery == 1);
    raw.battery = 255;
    CHECK(convert(raw).battery == 100);
    raw.accel[1] = 9.80665f;
    GamepadState s = convert(raw);
    CHECK(s.has_motion);
    CHECK_NEAR(s.accel[1], 1.0);
}

int main()
{
    RUN(test_buttons_by_position);
    RUN(test_sticks);
    RUN(test_triggers);
    RUN(test_battery_and_motion);
    DONE("input_bluepad32");
}
