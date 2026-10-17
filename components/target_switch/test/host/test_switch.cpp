#include <cstring>
#include <vector>

#include "check.h"
#include "yucca/settings.h"
#include "yucca/switch_target.h"

int g_verbose;
using namespace yucca;
namespace sp = yucca::switchpro;

// --- Fake transport --------------------------------------------------------

struct FakeBt {
    const bthid::Profile *profile = nullptr;
    bthid::Listener *listener = nullptr;
    btstack_packet_handler_t fallback = nullptr;
    bool host = false, connecting = false, connected = false, syncing = false, master = true;
    uint8_t reason = 0;
    int connect_calls = 0, disconnect_calls = 0, sync_calls = 0, forget_calls = 0, tune_calls = 0;
} g_bt;

namespace yucca::bthid {
void set_fallback_handler(btstack_packet_handler_t h) { g_bt.fallback = h; }
void begin(const Profile &p, Listener &l)
{
    g_bt.profile = &p;
    g_bt.listener = &l;
}
void sync()
{
    g_bt.sync_calls++;
    g_bt.syncing = true;
    g_bt.listener->on_sync(true);
}
bool syncing() { return g_bt.syncing; }
void connect()
{
    g_bt.connect_calls++;
    g_bt.connecting = true;
}
bool connecting() { return g_bt.connecting; }
bool connected() { return g_bt.connected; }
void disconnect() { g_bt.disconnect_calls++; }
bool has_host() { return g_bt.host; }
void forget()
{
    g_bt.forget_calls++;
    g_bt.host = false;
}
bool link_is_master() { return g_bt.connected && g_bt.master; }
uint8_t last_disconnect_reason() { return g_bt.reason; }
void flush() {}
void tune_link(hci_con_handle_t, const bd_addr_t) { g_bt.tune_calls++; }
void local_address(bd_addr_t out)
{
    for (int i = 0; i < 6; i++) {
        out[i] = uint8_t(0xa0 + i);
    }
}
}  // namespace yucca::bthid

// --- Helpers ---------------------------------------------------------------

struct Listener : TargetListener {
    int connected = 0, disconnected = 0, feedbacks = 0;
    bool syncing = false;
    Feedback feedback;
    void on_target_connected() override { connected++; }
    void on_target_disconnected() override { disconnected++; }
    void on_target_sync_changed(bool s) override { syncing = s; }
    void on_target_feedback(const Feedback &f) override
    {
        feedback = f;
        feedbacks++;
    }
};

static Listener listener;
static MemorySettings settings;
static int64_t now = 100000;

static void advance(SwitchTarget &t, int64_t ms)
{
    for (int64_t s = 0; s < ms; s += 20) {
        now += 20;
        t.tick(now);
    }
}

// A fresh target with a fresh fake transport.
static SwitchTarget *make(bool paired, SwitchTarget::Model model = SwitchTarget::Model::Switch, bool label = false)
{
    g_bt = FakeBt();
    g_bt.host = paired;
    listener = Listener();
    settings.set_int(keys::kFaceLayout, label ? 1 : 0);
    auto *t = new SwitchTarget();
    t->set_model(model);
    t->begin(listener, settings);
    t->tick(now);
    return t;
}

static InputLink classic_link()
{
    InputLink l;
    l.classic = true;
    l.handle = 0x81;
    return l;
}

static void link_up(SwitchTarget &t, bool master)
{
    g_bt.connecting = false;
    g_bt.connected = true;
    g_bt.master = master;
    g_bt.syncing = false;
    t.on_sync(false);
    t.on_link(true);
}

static void link_down(SwitchTarget &t, uint8_t reason)
{
    g_bt.connected = false;
    g_bt.reason = reason;
    t.on_link(false);
}

static const uint8_t kNeutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};

// Output report 0x01 with neutral rumble and a subcommand.
static void sub(SwitchTarget &t, uint8_t id, std::vector<uint8_t> args = {})
{
    static uint8_t counter = 0;
    std::vector<uint8_t> p = {0x01, uint8_t(counter++ & 0x0f)};
    p.insert(p.end(), kNeutral, kNeutral + 8);
    p.push_back(id);
    p.insert(p.end(), args.begin(), args.end());
    p.resize(49, 0);
    t.on_output(p.data(), p.size());
}

static uint8_t out[bthid::kMaxReport];

// The reply the transport would send next, or 0 when none.
static uint8_t next_reply(SwitchTarget &t)
{
    const size_t n = t.next_input(out, sizeof(out), false);
    return n == sp::kReportSize ? out[14] : 0;  // subcommand id echoed in the reply
}

// Run the console's handshake up to the player lights.
static void handshake(SwitchTarget &t)
{
    sub(t, 0x02);
    sub(t, 0x08, {0x00});
    sub(t, 0x03, {0x30});
    sub(t, 0x40, {0x01});
    sub(t, 0x48, {0x01});
    sub(t, 0x30, {0x01});
    while (t.next_input(out, sizeof(out), false) > 0) {
    }
}

// --- Tests -----------------------------------------------------------------

static void test_profile()
{
    SwitchTarget *t = make(false);
    CHECK(std::strcmp(t->id(), "switch") == 0 && std::strcmp(t->name(), "Switch") == 0);
    const bthid::Profile &p = *g_bt.profile;
    CHECK(std::strcmp(p.name, "Pro Controller") == 0 && p.cod == 0x002508);
    CHECK(p.descriptor_len == 170 && p.descriptor[0] == 0x05 && p.descriptor[169] == 0xc0);
    CHECK(p.subclass == 0x08 && p.country == 0x21 && p.pnp_vendor == 0x057e && p.pnp_product == 0x2009);
    CHECK(p.report_period_ms == 15);
    const uint32_t tag = p.host_tag;
    delete t;

    t = make(false, SwitchTarget::Model::Switch2);
    CHECK(std::strcmp(t->id(), "switch2") == 0 && std::strcmp(t->name(), "Switch 2") == 0);
    CHECK(g_bt.profile->host_tag != tag);  // each console remembered separately
    delete t;
}

static void test_first_run_opens_sync()
{
    SwitchTarget *t = make(false);
    t->on_input_ready(classic_link());
    CHECK(g_bt.tune_calls == 1 && g_bt.sync_calls == 1 && g_bt.connect_calls == 0);
    CHECK(t->syncing() && listener.syncing);
    delete t;
}

static void test_paired_connects()
{
    SwitchTarget *t = make(true);
    CHECK(t->has_paired_console());
    t->on_input_ready(classic_link());
    CHECK(g_bt.connect_calls == 1 && g_bt.sync_calls == 0);
    link_up(*t, true);
    CHECK(t->connected() && listener.connected == 1);
    advance(*t, 10000);
    CHECK(g_bt.disconnect_calls == 0);  // master: stays
    delete t;
}

static void test_no_controller_no_page()
{
    SwitchTarget *t = make(true);
    GamepadState s;
    s.set(Button::South, true);
    t->update(s);
    CHECK(g_bt.connect_calls == 0);
    // A console that pairs while no controller is there is let go at once.
    link_up(*t, false);
    CHECK(g_bt.disconnect_calls == 1 && listener.connected == 0 && !t->connected());
    link_down(*t, 0x16);
    CHECK(listener.disconnected == 0);  // never reported up, so never down
    delete t;
}

static void test_handshake_and_reports()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);

    // Replies go out in order, one per subcommand.
    sub(*t, 0x02);
    sub(*t, 0x08, {0x00});
    CHECK(next_reply(*t) == 0x02);
    CHECK(next_reply(*t) == 0x08);
    CHECK(next_reply(*t) == 0);
    // Simple mode: nothing periodic.
    CHECK(t->next_input(out, sizeof(out), true) == 0);

    // Full mode before the lights: at most one report per 60 ms.
    sub(*t, 0x03, {0x30});
    CHECK(next_reply(*t) == 0x03);
    advance(*t, 20);
    CHECK(t->next_input(out, sizeof(out), true) == sp::kReportSize && out[0] == 0x30);
    advance(*t, 20);
    CHECK(t->next_input(out, sizeof(out), true) == 0);
    advance(*t, 40);
    CHECK(t->next_input(out, sizeof(out), true) == sp::kReportSize);

    // Player lights: feedback, then every tick.
    sub(*t, 0x30, {0x01});
    CHECK(next_reply(*t) == 0x30);
    CHECK(listener.feedback.player_leds == 0x01);
    CHECK(t->next_input(out, sizeof(out), true) == sp::kReportSize);
    CHECK(t->next_input(out, sizeof(out), true) == sp::kReportSize);

    // A small buffer gets nothing rather than an overrun.
    sub(*t, 0x02);
    CHECK(t->next_input(out, 10, false) == 0);
    CHECK(next_reply(*t) == 0x02);
    delete t;
}

static void test_reply_queue_overflow()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    sub(*t, 0x02);
    for (int i = 0; i < 5; i++) {
        sub(*t, 0x08, {0x00});
    }
    // Oldest dropped: the newest four remain.
    int n = 0;
    while (next_reply(*t) != 0) {
        n++;
    }
    CHECK(n == 4);
    delete t;
}

static uint32_t report_buttons() { return out[3] | uint32_t(out[4]) << 8 | uint32_t(out[5]) << 16; }

static void test_buttons_and_sticks()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    handshake(*t);

    GamepadState s;
    s.set(Button::East, true);
    s.set(Button::L1, true);
    s.set(Button::Capture, true);
    s.set(Button::R3, true);
    s.l2 = 0.6f;
    s.r2 = 0.4f;
    s.lx = 1.0f;
    s.ly = -1.0f;
    s.battery = 50;
    t->update(s);
    CHECK(t->next_input(out, sizeof(out), true) == sp::kReportSize);
    const uint32_t b = report_buttons();
    CHECK(b == (sp::kA | sp::kL | sp::kCapture | sp::kRStick | sp::kZL));
    CHECK((out[2] >> 4) == 6);  // battery medium
    const int lx = out[6] | (out[7] & 0x0f) << 8, ly = out[7] >> 4 | out[8] << 4;
    CHECK(lx == sp::kStickCenter + sp::kStickRange && ly == sp::kStickCenter - sp::kStickRange);

    // Label layout: Xbox A (south) is console A.
    delete t;
    t = make(true, SwitchTarget::Model::Switch, true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    handshake(*t);
    s = GamepadState();
    s.set(Button::South, true);
    t->update(s);
    t->next_input(out, sizeof(out), true);
    CHECK(report_buttons() == sp::kA);

    // Controller gone: inputs released, link kept as master.
    t->on_input_gone();
    t->next_input(out, sizeof(out), true);
    CHECK(report_buttons() == 0 && g_bt.disconnect_calls == 0);
    delete t;
}

static void test_rumble_feedback()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    handshake(*t);
    const uint8_t buzz[10] = {0x10, 0, 0x00, 0xc8, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    t->on_output(buzz, sizeof(buzz));
    CHECK(listener.feedback.rumble_left > 0.9f && listener.feedback.rumble_right == 0.f);
    const int n = listener.feedbacks;
    t->on_output(buzz, sizeof(buzz));
    CHECK(listener.feedbacks == n);  // unchanged: not resent
    const uint8_t quiet[10] = {0x10, 0, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    t->on_output(quiet, sizeof(quiet));
    CHECK(listener.feedback.rumble_left == 0.f);
    // Link down clears lights and rumble.
    t->on_output(buzz, sizeof(buzz));
    link_down(*t, 0x13);
    CHECK(listener.feedback.player_leds == 0 && listener.feedback.rumble_left == 0.f);
    delete t;
}

static void test_slave_pairing_repages_as_master()
{
    SwitchTarget *t = make(false);
    t->on_input_ready(classic_link());
    CHECK(g_bt.sync_calls == 1);
    g_bt.host = true;  // the transport stores the console
    link_up(*t, false);
    CHECK(listener.connected == 1);
    advance(*t, 1000);
    CHECK(g_bt.disconnect_calls == 0);  // registering us
    handshake(*t);
    advance(*t, 1900);
    CHECK(g_bt.disconnect_calls == 0);  // lights set: a moment for L + R
    advance(*t, 200);
    CHECK(g_bt.disconnect_calls == 1);
    advance(*t, 200);
    CHECK(g_bt.disconnect_calls == 1);  // once
    link_down(*t, 0x16);
    CHECK(listener.disconnected == 1);
    advance(*t, 900);
    CHECK(g_bt.connect_calls == 0);
    advance(*t, 200);
    CHECK(g_bt.connect_calls == 1);
    // The first page fails (console still busy): retried.
    g_bt.connecting = false;
    advance(*t, 1200);
    CHECK(g_bt.connect_calls == 2);
    link_up(*t, true);
    CHECK(listener.connected == 2);
    advance(*t, 10000);
    CHECK(g_bt.disconnect_calls == 1);  // master now: stays
    delete t;
}

static void test_slave_grace_timeout()
{
    SwitchTarget *t = make(false);
    t->on_input_ready(classic_link());
    g_bt.host = true;
    link_up(*t, false);
    advance(*t, 14900);
    CHECK(g_bt.disconnect_calls == 0);
    advance(*t, 200);
    CHECK(g_bt.disconnect_calls == 1);
    delete t;
}

static void test_ack_wait_capped()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    handshake(*t);
    sub(*t, 0x06, {0x00});  // the channel never frees up
    advance(*t, 160);
    CHECK(g_bt.disconnect_calls == 0);
    advance(*t, 60);
    CHECK(g_bt.disconnect_calls == 1);
    delete t;
}

static void test_console_sleep()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    handshake(*t);
    sub(*t, 0x06, {0x00});
    advance(*t, 100);
    CHECK(g_bt.disconnect_calls == 0);  // ACK still queued
    CHECK(next_reply(*t) == 0x06);      // ACK goes out first
    advance(*t, 20);
    CHECK(g_bt.disconnect_calls == 1);
    link_down(*t, 0x16);
    advance(*t, 70000);
    CHECK(g_bt.connect_calls == 1);  // no auto-reconnect to a sleeping console
    // A button press wakes it.
    GamepadState s;
    s.set(Button::Home, true);
    t->update(s);
    CHECK(g_bt.connect_calls == 2);
    delete t;
}

static void test_auto_reconnect()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    link_down(*t, 0x08);  // supervision timeout
    advance(*t, 300);
    CHECK(g_bt.connect_calls == 2);
    g_bt.connecting = false;  // page failed
    advance(*t, 1200);
    CHECK(g_bt.connect_calls == 3);
    g_bt.connecting = false;
    advance(*t, 70000);
    const int calls = g_bt.connect_calls;
    g_bt.connecting = false;
    advance(*t, 5000);
    CHECK(g_bt.connect_calls == calls);  // window over
    delete t;
}

static void test_sync_and_forget()
{
    SwitchTarget *t = make(true);
    t->on_input_ready(classic_link());
    link_up(*t, true);
    t->start_sync();
    CHECK(g_bt.disconnect_calls == 1 && g_bt.sync_calls == 1);
    link_down(*t, 0x16);
    advance(*t, 70000);
    CHECK(g_bt.connect_calls == 1);  // our own hang-up: no auto-reconnect
    t->forget();
    CHECK(g_bt.forget_calls == 1 && !t->has_paired_console());
    delete t;
}

int main()
{
    RUN(test_profile);
    RUN(test_first_run_opens_sync);
    RUN(test_paired_connects);
    RUN(test_no_controller_no_page);
    RUN(test_handshake_and_reports);
    RUN(test_reply_queue_overflow);
    RUN(test_buttons_and_sticks);
    RUN(test_rumble_feedback);
    RUN(test_slave_pairing_repages_as_master);
    RUN(test_slave_grace_timeout);
    RUN(test_console_sleep);
    RUN(test_ack_wait_capped);
    RUN(test_auto_reconnect);
    RUN(test_sync_and_forget);
    DONE("target_switch");
}
