#include "../../../yucca_core/test/host/check.h"
#include "yucca/app.h"
#include "yucca/settings.h"

using namespace yucca;

int g_verbose = 0;

struct FakeInput : InputDriver {
    InputListener *listener = nullptr;
    bool is_ready = false;
    int scan_calls = 0;
    bool scanning = false;
    int forgets = 0;
    Feedback feedback;
    void begin(InputListener &l) override { listener = &l; }
    bool ready() const override { return is_ready; }
    void set_scanning(bool on) override
    {
        scan_calls++;
        scanning = on;
    }
    void apply(const Feedback &f) override { feedback = f; }
    void forget_all() override
    {
        forgets++;
        if (is_ready) {
            is_ready = false;
            listener->on_input_gone();
        }
    }
    void tick(int64_t) override {}
};

struct FakeTarget : Target {
    TargetListener *listener = nullptr;
    bool is_syncing = false;
    int syncs = 0, forgets = 0, ready = 0, gone = 0, updates = 0;
    const char *id() const override { return "fake"; }
    const char *name() const override { return "Fake"; }
    void set_fallback_handler(PacketHandler) override {}
    void begin(TargetListener &l, Settings &) override { listener = &l; }
    bool has_paired_console() const override { return true; }
    bool connected() const override { return false; }
    bool syncing() const override { return is_syncing; }
    void start_sync() override
    {
        syncs++;
        is_syncing = true;
        listener->on_target_sync_changed(true);
    }
    void forget() override { forgets++; }
    void on_input_ready(const InputLink &) override { ready++; }
    void on_input_gone() override { gone++; }
    void update(const GamepadState &) override { updates++; }
    void tick(int64_t) override {}
};

static const SettingChoice kTargets[] = {{"wii", "Wii"}, {"wiiu", "Wii U"}, {"switch", "Switch"}};
static int g_restarts = 0;
static void fake_restart() { g_restarts++; }

struct Rig {
    MemorySettings settings;
    FakeInput input;
    FakeTarget target;
    App app{settings, input, target, kTargets, 3, fake_restart};
    int64_t now = 0;
    Rig() { app.begin(); }
    void run(int64_t ms, bool boot = false)
    {
        for (int64_t t = 0; t < ms; t += 20) {
            now += 20;
            app.tick(now, boot);
        }
    }
    void controller_ready()
    {
        input.is_ready = true;
        app.on_input_ready(InputLink{});
    }
};

static void test_scans_until_controller_ready()
{
    Rig r;
    CHECK(r.input.scanning);
    r.controller_ready();
    CHECK(!r.input.scanning);
    CHECK(r.target.ready == 1);
    r.input.is_ready = false;
    r.app.on_input_gone();
    CHECK(r.input.scanning);
    CHECK(r.target.gone == 1);
}

static void test_no_scan_while_console_syncs()
{
    Rig r;
    r.target.start_sync();
    CHECK(!r.input.scanning);
    r.target.is_syncing = false;
    r.app.on_target_sync_changed(false);
    CHECK(r.input.scanning);
}

static void test_select_start_hold_syncs_once()
{
    Rig r;
    r.controller_ready();
    GamepadState s;
    s.set(Button::Select, true);
    s.set(Button::Start, true);
    r.app.on_input_state(s);
    r.run(2900);
    CHECK(r.target.syncs == 0);
    r.run(200);
    CHECK(r.target.syncs == 1);
    r.target.is_syncing = false;
    r.run(5000);
    CHECK(r.target.syncs == 1);  // once per hold
    CHECK(r.target.updates == 1);
}

static void test_boot_tap_syncs_and_hold_forgets()
{
    Rig r;
    r.run(100, true);
    r.run(20, false);
    CHECK(r.target.syncs == 1);
    r.target.is_syncing = false;

    r.run(5100, true);
    CHECK(r.target.forgets == 1);
    CHECK(r.input.forgets == 1);
    r.run(20, false);
    CHECK(r.target.syncs == 1);  // the release after a long hold does not sync
}

static void test_feedback_reaches_controller()
{
    Rig r;
    Feedback f;
    f.player_leds = 0x2;
    r.app.on_target_feedback(f);
    CHECK(r.input.feedback.player_leds == 0x2);
}

static void test_forget_tells_target_controller_is_gone()
{
    Rig r;
    r.controller_ready();
    r.app.forget_everything();
    CHECK(r.target.forgets == 1);
    CHECK(r.target.gone == 1);
    CHECK(r.input.scanning);
}

static void test_combo_waits_for_open_sync_window()
{
    Rig r;
    r.controller_ready();
    r.target.is_syncing = true;
    GamepadState s;
    s.set(Button::Select, true);
    s.set(Button::Start, true);
    r.app.on_input_state(s);
    r.run(3100);
    CHECK(r.target.syncs == 0);
    r.target.is_syncing = false;
    r.run(40);
    CHECK(r.target.syncs == 1);
}

static void press(Rig &r, Button b)
{
    GamepadState s;
    s.set(b, true);
    r.app.on_input_state(s);
    r.app.on_input_state(GamepadState{});
}

static void open_menu(Rig &r)
{
    r.controller_ready();
    GamepadState s;
    s.set(Button::Select, true);
    s.set(Button::Home, true);
    r.app.on_input_state(s);
    r.run(3100);
    CHECK(r.app.menu_open());
    r.app.on_input_state(GamepadState{});  // let go of the combo
}

static void test_menu_switches_console_and_restarts()
{
    g_restarts = 0;
    Rig r;
    open_menu(r);
    CHECK(r.input.feedback.player_leds == 0x1);  // Wii (default)
    int updates = r.target.updates;
    press(r, Button::DpadRight);
    CHECK(r.input.feedback.player_leds == 0x2);
    CHECK(r.input.feedback.rumble_left > 0);
    r.run(200);
    CHECK_NEAR(r.input.feedback.rumble_left, 0.0);  // pulse ended
    press(r, Button::DpadRight);
    CHECK(r.input.feedback.player_leds == 0x4);
    CHECK(r.target.updates == updates);  // nothing reached the console
    press(r, Button::South);
    CHECK(r.settings.get_str(keys::kTarget, "wii") == "switch");
    CHECK(g_restarts == 1);
}

static void test_menu_wraps_and_cancels()
{
    g_restarts = 0;
    Rig r;
    open_menu(r);
    press(r, Button::DpadLeft);
    CHECK(r.input.feedback.player_leds == 0x4);  // wrapped to the last
    Feedback f;
    f.player_leds = 0x8;
    r.app.on_target_feedback(f);                 // held back while the menu is open
    CHECK(r.input.feedback.player_leds == 0x4);
    press(r, Button::Select);
    CHECK(!r.app.menu_open());
    CHECK(r.input.feedback.player_leds == 0x8);  // console's LEDs restored
    CHECK(g_restarts == 0);
    CHECK(r.settings.get_str(keys::kTarget, "wii") == "wii");
}

static void test_menu_same_console_and_timeout()
{
    g_restarts = 0;
    Rig r;
    open_menu(r);
    press(r, Button::East);  // confirm the current console: just close
    CHECK(!r.app.menu_open());
    CHECK(g_restarts == 0);
    GamepadState s;
    s.set(Button::Select, true);
    s.set(Button::Home, true);
    r.app.on_input_state(s);
    r.run(3100);
    CHECK(r.app.menu_open());
    r.app.on_input_state(GamepadState{});
    r.run(15100);
    CHECK(!r.app.menu_open());
}

static void test_console_requests_run_on_tick()
{
    Rig r;
    r.app.request_sync();
    CHECK(r.target.syncs == 0);
    r.run(20);
    CHECK(r.target.syncs == 1);
    r.app.request_forget();
    r.run(20);
    CHECK(r.target.forgets == 1);
}

int main()
{
    RUN(test_console_requests_run_on_tick);
    RUN(test_menu_switches_console_and_restarts);
    RUN(test_menu_wraps_and_cancels);
    RUN(test_menu_same_console_and_timeout);
    RUN(test_forget_tells_target_controller_is_gone);
    RUN(test_combo_waits_for_open_sync_window);
    RUN(test_scans_until_controller_ready);
    RUN(test_no_scan_while_console_syncs);
    RUN(test_select_start_hold_syncs_once);
    RUN(test_boot_tap_syncs_and_hold_forgets);
    RUN(test_feedback_reaches_controller);
    DONE("yucca_app");
}
