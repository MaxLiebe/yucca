#include "check.h"
#include "yucca/gamepad.h"
#include "yucca/mapper.h"
#include "yucca/settings.h"
#include "yucca/settings_schema.h"

using namespace yucca;

static void test_deadzone()
{
    float x = 0.05f, y = 0.0f;
    apply_radial_deadzone(x, y, 0.08f);
    CHECK_NEAR(x, 0.0);
    CHECK_NEAR(y, 0.0);

    x = 1.0f;
    y = 0.0f;
    apply_radial_deadzone(x, y, 0.08f);
    CHECK_NEAR(x, 1.0);

    // Just past the deadzone starts near zero, not at 0.08.
    x = 0.09f;
    y = 0.0f;
    apply_radial_deadzone(x, y, 0.08f);
    CHECK(x > 0.0f && x < 0.02f);

    // Corners are clamped to the unit circle and keep their direction.
    x = 1.0f;
    y = 1.0f;
    apply_radial_deadzone(x, y, 0.08f);
    CHECK_NEAR(x * x + y * y, 1.0);
    CHECK_NEAR(x, y);
}

static void test_buttons()
{
    GamepadState s;
    CHECK(!s.any_input());
    s.set(Button::Home, true);
    CHECK(s.pressed(Button::Home));
    CHECK(s.any_input());
    s.set(Button::Home, false);
    CHECK(!s.pressed(Button::Home));
}

enum : uint32_t { kA = 1, kB = 2, kX = 4, kY = 8, kHome = 16 };
static const ButtonRoute kRoutes[] = {
    {Button::East, kA},  {Button::South, kB}, {Button::North, kX},
    {Button::West, kY},  {Button::Home, kHome},
};

static void test_mapper_position()
{
    Mapper m(kRoutes, 5);
    GamepadState s;
    s.set(Button::East, true);  // DualSense Circle / Xbox B
    CHECK(m.map(s) == kA);
    s = {};
    s.set(Button::South, true);
    CHECK(m.map(s) == kB);
    s.set(Button::Home, true);
    CHECK(m.map(s) == (kB | kHome));
}

static void test_mapper_label()
{
    Mapper m(kRoutes, 5);
    m.set_layout(FaceLayout::Label);
    GamepadState s;
    s.set(Button::South, true);  // Xbox A -> console A
    CHECK(m.map(s) == kA);
    s = {};
    s.set(Button::East, true);  // Xbox B -> console B
    CHECK(m.map(s) == kB);
    s = {};
    s.set(Button::West, true);  // Xbox X -> console X
    CHECK(m.map(s) == kX);
    s = {};
    s.set(Button::North, true);  // Xbox Y -> console Y
    CHECK(m.map(s) == kY);
    s = {};
    s.set(Button::Home, true);  // non-face buttons are untouched
    CHECK(m.map(s) == kHome);
}

static void test_settings()
{
    MemorySettings st;
    CHECK(st.get_int(keys::kBle, 1) == 1);
    st.set_int(keys::kBle, 0);
    CHECK(st.get_int(keys::kBle, 1) == 0);
    CHECK(st.get_str(keys::kTarget, "wii") == "wii");
    st.set_str(keys::kTarget, "switch");
    CHECK(st.get_str(keys::kTarget, "wii") == "switch");
    st.erase(keys::kTarget);
    CHECK(st.get_str(keys::kTarget, "wii") == "wii");
    st.commit();
    CHECK(st.commits() == 1);
}

static void test_schema()
{
    static const SettingChoice targets[] = {{"wii", "Wii"}, {"switch", "Switch"}};
    schema_init(targets, 2);
    MemorySettings st;
    CHECK(schema_set(st, "nope", "1") != nullptr);
    CHECK(schema_set(st, keys::kBle, "2") != nullptr);
    CHECK(schema_set(st, keys::kBle, "x") != nullptr);
    CHECK(schema_set(st, keys::kBle, "0") == nullptr);
    CHECK(st.get_int(keys::kBle, 1) == 0);
    CHECK(schema_set(st, keys::kTarget, "ps5") != nullptr);
    CHECK(schema_set(st, keys::kTarget, "switch") == nullptr);
    CHECK(st.get_str(keys::kTarget, "") == "switch");
    bool restart = false;
    CHECK(schema_set(st, keys::kFaceLayout, "1", &restart) == nullptr);
    CHECK(restart);  // targets read face_layout at boot
    restart = true;
    CHECK(schema_set(st, "nope", "1", &restart) != nullptr);
    CHECK(restart);  // untouched on error
    CHECK(st.get_int(keys::kFaceLayout, 0) == 1);  // stored as int for the targets
    const SettingSpec *spec = schema_find("wii_mirror");
    CHECK(spec != nullptr && schema_get(st, *spec) == "1");  // default
    std::string json = schema_json(st);
    CHECK(json.find("\"target\":{\"label\":\"Console\"") != std::string::npos);
    CHECK(json.find("{\"value\":\"switch\",\"label\":\"Switch\"}") != std::string::npos);
    CHECK(json.find("\"value\":\"switch\"}") != std::string::npos);
    CHECK(json.front() == '{' && json.back() == '}');
}

int main()
{
    RUN(test_schema);
    RUN(test_deadzone);
    RUN(test_buttons);
    RUN(test_mapper_position);
    RUN(test_mapper_label);
    RUN(test_settings);
    DONE("yucca_core");
}
