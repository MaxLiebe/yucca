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
    void forget_all() override { forgets++; }
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

struct Rig {
    MemorySettings settings;
    FakeInput input;
    FakeTarget target;
    App app{settings, input, target};
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

int main()
{
    RUN(test_scans_until_controller_ready);
    RUN(test_no_scan_while_console_syncs);
    RUN(test_select_start_hold_syncs_once);
    RUN(test_boot_tap_syncs_and_hold_forgets);
    RUN(test_feedback_reaches_controller);
    DONE("yucca_app");
}
