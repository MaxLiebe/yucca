#include <string>

#include "../../../yucca_core/test/host/check.h"
#include "fake_wiimote.h"
#include "yucca/settings.h"
#include "yucca/wii_target.h"

using namespace yucca;

struct Listener : TargetListener {
    int connected = 0, disconnected = 0;
    bool syncing = false;
    Feedback feedback;
    void on_target_connected() override { connected++; }
    void on_target_disconnected() override { disconnected++; }
    void on_target_sync_changed(bool s) override { syncing = s; }
    void on_target_feedback(const Feedback &f) override { feedback = f; }
};

// WiiTarget is a singleton: each test drives it back to a known state.
static WiiTarget &wii = WiiTarget::instance();
static Listener listener;
static MemorySettings settings;
static int64_t now = 100000;

static void advance(int64_t ms)
{
    for (int64_t t = 0; t < ms; t += 20) {
        now += 20;
        wii.tick(now);
    }
}

static void reset(bool paired, bool label = false)
{
    fake_reset();
    g_wm.paired = paired;
    listener = Listener();
    settings.set_int(keys::kFaceLayout, label ? 1 : 0);
    wii.begin(listener, settings);
    // Drop whatever the previous test left behind.
    if (wii.connected()) {
        g_wm.connected = false;
        wii.handle_disconnected();
    }
    wii.on_input_gone();
    wii.handle_sync(false);
    advance(70000);
    fake_reset();
    g_wm.paired = paired;
    g_wm.callbacks = nullptr;
}

static void connect_wii()
{
    g_wm.connecting = false;
    g_wm.connected = true;
    wii.handle_connected();
}

static InputLink classic_link()
{
    InputLink l;
    l.classic = true;
    l.handle = 0x81;
    return l;
}

static void test_first_run_opens_sync()
{
    reset(false);
    wii.on_input_ready(classic_link());
    CHECK(g_wm.tune_calls == 1);
    CHECK(g_wm.sync_calls == 1);
    CHECK(g_wm.connect_calls == 0);
}

static void test_paired_connects_when_controller_ready()
{
    reset(true);
    wii.on_input_ready(classic_link());
    CHECK(g_wm.sync_calls == 0);
    CHECK(g_wm.connect_calls == 1);
    connect_wii();
    CHECK(wii.connected());
    CHECK(listener.connected == 1);
}

static void test_no_page_without_controller()
{
    reset(true);
    GamepadState s;
    s.set(Button::South, true);
    wii.update(s);
    CHECK(g_wm.connect_calls == 0);
}

static void test_slave_link_is_repaged_as_master()
{
    reset(true);
    wii.on_input_ready(classic_link());
    g_wm.master = false;
    connect_wii();
    CHECK(g_wm.disconnect_calls == 1);
    CHECK(listener.connected == 0);
    g_wm.connected = false;
    wii.handle_disconnected();
    int before = g_wm.connect_calls;
    g_wm.connecting = false;
    advance(500);
    CHECK(g_wm.connect_calls == before);
    advance(600);
    CHECK(g_wm.connect_calls == before + 1);
}

static void test_auto_reconnect_after_wii_drops()
{
    reset(true);
    wii.on_input_ready(classic_link());
    connect_wii();
    g_wm.reason = 0x13;  // remote user terminated (a game reset the remotes)
    g_wm.connected = false;
    wii.handle_disconnected();
    CHECK(listener.disconnected == 1);
    int before = g_wm.connect_calls;
    advance(240);
    CHECK(g_wm.connect_calls == before + 1);  // first page after 200 ms
    g_wm.connecting = false;                  // page timed out
    advance(140);
    CHECK(g_wm.connect_calls == before + 2);  // back to back
    // Gives up after 60 s.
    for (int i = 0; i < 700; i++) {
        g_wm.connecting = false;
        advance(100);
    }
    int at_end = g_wm.connect_calls;
    g_wm.connecting = false;
    advance(2000);
    CHECK(g_wm.connect_calls == at_end);
}

static void test_power_off_reason_shortens_window()
{
    reset(true);
    wii.on_input_ready(classic_link());
    connect_wii();
    g_wm.reason = 0x15;
    g_wm.connected = false;
    wii.handle_disconnected();
    for (int i = 0; i < 160; i++) {
        g_wm.connecting = false;
        advance(100);
    }
    int at_end = g_wm.connect_calls;
    g_wm.connecting = false;
    advance(2000);
    CHECK(g_wm.connect_calls == at_end);
}

static void test_button_press_retries_at_most_once_a_second()
{
    reset(true);
    wii.on_input_ready(classic_link());  // first attempt
    g_wm.connecting = false;
    GamepadState s;
    s.set(Button::Start, true);
    wii.update(s);
    CHECK(g_wm.connect_calls == 1);
    advance(1020);
    wii.update(s);
    CHECK(g_wm.connect_calls == 2);
}

static void test_mapping_by_position()
{
    reset(true);
    GamepadState s;
    s.set(Button::East, true);  // DualSense Circle
    s.set(Button::L1, true);
    s.set(Button::Home, true);
    s.l2 = 0.9f;
    s.r2 = 0.5f;
    s.lx = -1.0f;
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_B]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_A]);  // mirrored on the remote
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZL]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_HOME]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_HOME]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_L]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_R]);
    CHECK_NEAR(g_wm.axes[WIIMOTE_AXIS_CLASSIC_RT], 0.5);
    CHECK_NEAR(g_wm.axes[WIIMOTE_AXIS_CLASSIC_LX], -1.0);
    CHECK(g_wm.send_calls == 1);

    s = {};
    s.set(Button::West, true);   // Classic y -> remote 1
    s.set(Button::North, true);  // Classic x -> remote 2
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_Y]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_X]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_ONE]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_TWO]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_A]);
}

static void test_mapping_by_label()
{
    reset(true, true);
    GamepadState s;
    s.set(Button::South, true);  // Xbox A -> Classic a
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_B]);
}

static void test_controller_gone_keeps_master_link()
{
    reset(true);
    wii.on_input_ready(classic_link());
    connect_wii();
    GamepadState s;
    s.set(Button::East, true);
    wii.update(s);
    wii.on_input_gone();
    CHECK(g_wm.disconnect_calls == 0);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A]);  // released
}

static void test_controller_gone_drops_slave_link()
{
    reset(true);
    wii.on_input_ready(classic_link());
    connect_wii();
    g_wm.master = false;
    wii.on_input_gone();
    CHECK(g_wm.disconnect_calls == 1);
}

static void test_feedback()
{
    reset(true);
    wii.on_input_ready(classic_link());
    connect_wii();
    wii.handle_leds(0x1);
    CHECK(listener.feedback.player_leds == 0x1);
    wii.handle_rumble(true);
    CHECK(listener.feedback.rumble_left > 0.9f);
    g_wm.connected = false;
    wii.handle_disconnected();
    CHECK(listener.feedback.player_leds == 0);
    CHECK_NEAR(listener.feedback.rumble_left, 0.0);
}

static void test_battery()
{
    reset(true);
    GamepadState s;
    s.battery = 100;
    wii.update(s);
    CHECK(g_wm.battery == 0xC8);
    s.battery = 0;
    wii.update(s);
    CHECK(g_wm.battery == 0x10);
}

static void begin_model(WiiTarget::Model model, int wiiu_mode)
{
    wii.set_model(model);
    settings.set_int(WiiTarget::kWiiuModeKey, wiiu_mode);
    reset(true);
    wii.begin(listener, settings);
}

static void test_wii_identity()
{
    begin_model(WiiTarget::Model::Wii, 0);
    CHECK(std::string(wii.id()) == "wii");
    CHECK(g_wm.extension == WIIMOTE_EXT_CLASSIC);
    CHECK(!g_wm.disable_ssp);  // gamepads keep SSP on a Wii
    CHECK(!wii.pro());
}

static void test_wiiu_pro_identity_and_mapping()
{
    begin_model(WiiTarget::Model::WiiU, 0);
    CHECK(std::string(wii.id()) == "wiiu");
    CHECK(std::string(wii.name()) == "Wii U");
    CHECK(g_wm.extension == WIIMOTE_EXT_WIIU_PRO);
    CHECK(g_wm.disable_ssp);
    CHECK(wii.pro());

    GamepadState s;
    s.set(Button::East, true);
    s.set(Button::L1, true);
    s.set(Button::R3, true);
    s.set(Button::Home, true);
    s.l2 = 0.6f;
    s.r2 = 0.4f;
    s.ry = 0.25f;
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_L]);   // shoulder
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_R]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZL]);  // trigger past half
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZR]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_PRO_RSTICK]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_PRO_LSTICK]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_HOME]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_A]);     // no Wii Remote buttons on a Pro
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_HOME]);
    CHECK_NEAR(g_wm.axes[WIIMOTE_AXIS_CLASSIC_RY], 0.25);
    CHECK(g_wm.send_calls == 1);
}

static void test_wiiu_classic_fallback()
{
    begin_model(WiiTarget::Model::WiiU, 1);
    CHECK(g_wm.extension == WIIMOTE_EXT_CLASSIC);
    CHECK(g_wm.disable_ssp);  // still a Wii U
    CHECK(!wii.pro());
    GamepadState s;
    s.set(Button::L1, true);
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZL]);  // Classic layout
    wii.set_model(WiiTarget::Model::Wii);
    settings.set_int(WiiTarget::kWiiuModeKey, 0);
}

static void test_bond_follows_identity()
{
    // Paired as a Wii Remote with a Wii, then switched to the Wii U.
    begin_model(WiiTarget::Model::Wii, 0);
    settings.erase(WiiTarget::kBondKey);
    g_wm.connected = true;
    wii.on_input_ready(classic_link());
    connect_wii();
    CHECK(settings.get_int(WiiTarget::kBondKey, -1) == WiiTarget::kWiiRemote);
    g_wm.connected = false;
    wii.handle_disconnected();

    wii.set_model(WiiTarget::Model::WiiU);
    fake_reset();
    g_wm.paired = true;
    wii.begin(listener, settings);
    CHECK(g_wm.forget_calls == 1);  // the Wii is forgotten, so the Wii U can sync
    fake_reset();
    g_wm.paired = true;
    settings.set_int(WiiTarget::kBondKey, WiiTarget::kWiiuPro);
    wii.begin(listener, settings);
    CHECK(g_wm.forget_calls == 0);  // same console and identity: keep it
    settings.set_int(WiiTarget::kWiiuModeKey, 1);
    fake_reset();
    g_wm.paired = true;
    wii.begin(listener, settings);
    CHECK(g_wm.forget_calls == 1);  // Wii U paired with the Pro, now a Remote
    wii.set_model(WiiTarget::Model::Wii);
    settings.set_int(WiiTarget::kWiiuModeKey, 0);
    settings.erase(WiiTarget::kBondKey);
}

static void test_wiiu_pro_layout_and_release()
{
    begin_model(WiiTarget::Model::WiiU, 0);
    settings.set_int(keys::kFaceLayout, 1);
    wii.begin(listener, settings);
    GamepadState s;
    s.set(Button::South, true);  // Xbox A -> Nintendo A
    s.set(Button::R1, true);
    s.set(Button::L3, true);
    s.r2 = 1.0f;
    wii.update(s);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_R]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_PRO_LSTICK]);
    CHECK(g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZR]);
    wii.on_input_gone();  // releases everything
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_A] && !g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_ZR]);
    CHECK(!g_wm.buttons[WIIMOTE_BUTTON_PRO_LSTICK] && !g_wm.buttons[WIIMOTE_BUTTON_CLASSIC_R]);
    settings.set_int(keys::kFaceLayout, 0);
    wii.set_model(WiiTarget::Model::Wii);
}

int main()
{
    RUN(test_first_run_opens_sync);
    RUN(test_paired_connects_when_controller_ready);
    RUN(test_no_page_without_controller);
    RUN(test_slave_link_is_repaged_as_master);
    RUN(test_auto_reconnect_after_wii_drops);
    RUN(test_power_off_reason_shortens_window);
    RUN(test_button_press_retries_at_most_once_a_second);
    RUN(test_mapping_by_position);
    RUN(test_mapping_by_label);
    RUN(test_controller_gone_keeps_master_link);
    RUN(test_controller_gone_drops_slave_link);
    RUN(test_feedback);
    RUN(test_battery);
    RUN(test_wii_identity);
    RUN(test_wiiu_pro_identity_and_mapping);
    RUN(test_wiiu_classic_fallback);
    RUN(test_bond_follows_identity);
    RUN(test_wiiu_pro_layout_and_release);
    DONE("target_wii");
}
