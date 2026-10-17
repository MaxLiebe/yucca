#include "yucca/wii_target.h"

#include "esp_log.h"
#include "wiimote.h"
#include "bluetooth.h"  // hci_con_handle_t, bd_addr_t for wiimote_btstack.h
#include "wiimote_btstack.h"
#include "yucca/settings.h"

namespace yucca {

static const char *TAG = "wii";

// Button presses retry a failed page at most this often.
static constexpr int64_t kReconnectRetryMs = 1000;
// After the Wii dropped us (a game or homebrew app starting resets every
// remote), page it again on our own, back to back, for this long, like a real
// remote does. A Wii that "powered off" (reason 0x15) gets a shorter window:
// IOS reloads report that too, and paging a console that really is off costs
// nothing but radio time.
static constexpr int64_t kAutoReconnectWindowMs = 60000;
static constexpr int64_t kAutoReconnectPowerOffMs = 15000;
static constexpr int64_t kAutoReconnectFirstMs = 200;
static constexpr int64_t kAutoReconnectGapMs = 100;
// Re-page this soon after dropping a link on which the ESP32 is the slave.
static constexpr int64_t kRepageDelayMs = 1000;
static constexpr uint8_t kReasonPowerOff = 0x15;
// Classic Controller L/R click at the end of the analog travel.
static constexpr float kTriggerClick = 0.85f;

// Classic Controller buttons the mapper routes; L/R come from the triggers.
enum : uint32_t {
    kCcUp = 1 << 0,
    kCcDown = 1 << 1,
    kCcLeft = 1 << 2,
    kCcRight = 1 << 3,
    kCcA = 1 << 4,
    kCcB = 1 << 5,
    kCcX = 1 << 6,
    kCcY = 1 << 7,
    kCcPlus = 1 << 8,
    kCcMinus = 1 << 9,
    kCcHome = 1 << 10,
    kCcZL = 1 << 11,
    kCcZR = 1 << 12,
};

// Classic Controller is a Nintendo layout: a east, b south, x north, y west.
// ZL/ZR are the small inner buttons (controller L1/R1); L/R the triggers.
static const ButtonRoute kRoutes[] = {
    {Button::DpadUp, kCcUp},    {Button::DpadDown, kCcDown}, {Button::DpadLeft, kCcLeft},
    {Button::DpadRight, kCcRight}, {Button::East, kCcA},      {Button::South, kCcB},
    {Button::North, kCcX},      {Button::West, kCcY},        {Button::Start, kCcPlus},
    {Button::Select, kCcMinus}, {Button::Home, kCcHome},     {Button::L1, kCcZL},
    {Button::R1, kCcZR},
};

extern "C" {
static void cb_connected(void *) { WiiTarget::instance().handle_connected(); }
static void cb_disconnected(void *) { WiiTarget::instance().handle_disconnected(); }
static void cb_leds(uint8_t mask, void *) { WiiTarget::instance().handle_leds(mask); }
static void cb_rumble(bool on, void *) { WiiTarget::instance().handle_rumble(on); }
static void cb_sync(bool active, void *) { WiiTarget::instance().handle_sync(active); }
}

static const wiimote_callbacks_t kCallbacks = {
    .connected = cb_connected,
    .disconnected = cb_disconnected,
    .leds = cb_leds,
    .rumble = cb_rumble,
    .battery = nullptr,
    .extension_changed = nullptr,
    .user = nullptr,
    .sync_changed = cb_sync,
};

WiiTarget &WiiTarget::instance()
{
    static WiiTarget target;
    return target;
}

WiiTarget::WiiTarget()
    : mapper_(kRoutes, sizeof(kRoutes) / sizeof(kRoutes[0])), last_connect_ms_(-kReconnectRetryMs)
{
}

void WiiTarget::begin(TargetListener &listener, Settings &settings)
{
    listener_ = &listener;
    mapper_.set_layout(static_cast<FaceLayout>(settings.get_int(keys::kFaceLayout, 0)));
    mirror_core_ = settings.get_int(kMirrorKey, 1) != 0;

    // WiimoteAnything owns the HID PSMs; everything that is not the Wii goes
    // to the controller side.
    wiimote_btstack_set_fallback_handler(fallback_);
    wiimote_config_t cfg = {};
    cfg.extension = WIIMOTE_EXT_CLASSIC;
    cfg.battery = 0xC0;
    cfg.callbacks = &kCallbacks;
    cfg.external_stack = true;
    if (wiimote_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "WiimoteAnything failed to start");
    }

    if (wiimote_has_paired_host()) {
        ESP_LOGI(TAG, "Ready. Turn your controller on; YUCCA then connects to the Wii.");
    } else {
        ESP_LOGI(TAG, "First run: pair the controller now; the Wii sync window opens afterwards.");
    }
}

bool WiiTarget::has_paired_console() const { return wiimote_has_paired_host(); }

void WiiTarget::start_sync()
{
    ESP_LOGI(TAG, "Sync: press the red SYNC button on the Wii now (%d s)", WIIMOTE_SYNC_WINDOW_MS / 1000);
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_) {
        we_dropped_ = true;
        wiimote_disconnect();
    }
    wiimote_sync();
}

void WiiTarget::forget()
{
    ESP_LOGW(TAG, "Forgetting the Wii");
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_) {
        we_dropped_ = true;
    }
    wiimote_forget_host();
}

void WiiTarget::try_connect(const char *why)
{
    if (connected_ || syncing_ || !wiimote_has_paired_host() || wiimote_is_connecting()) {
        return;
    }
    if (!input_present_) {
        // Paging the Wii without a controller would only leave us as its slave.
        return;
    }
    if (now_ - last_connect_ms_ < kReconnectRetryMs) {
        return;
    }
    last_connect_ms_ = now_;
    ESP_LOGI(TAG, "Connecting to the Wii (%s)", why);
    wiimote_connect();
}

void WiiTarget::on_input_ready(const InputLink &link)
{
    input_present_ = true;
    battery_sent_ = -2;
    if (link.classic) {
        // ESP32 master, no sniff, short poll interval, so the controller link
        // shares the radio well with the Wii link.
        wiimote_btstack_tune_link(link.handle, link.addr);
    }
    last_connect_ms_ = now_ - kReconnectRetryMs;  // allow an immediate attempt
    if (!wiimote_has_paired_host() && !syncing_) {
        ESP_LOGI(TAG, "No Wii paired yet: opening the sync window.");
        start_sync();
    } else {
        try_connect("controller ready");
    }
}

void WiiTarget::on_input_gone()
{
    input_present_ = false;
    release_inputs();
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_ && !wiimote_link_is_master()) {
        // Only as the Wii's slave would the link block the controller from
        // coming back. As master, keep it: dropping a remote mid-game makes
        // some games hang on the "reconnect" screen. Inputs are released, so
        // the Wii simply sees an idle remote until the controller returns.
        ESP_LOGI(TAG, "Releasing the Wii until the controller is back");
        we_dropped_ = true;
        wiimote_disconnect();
    } else if (connected_) {
        ESP_LOGI(TAG, "Keeping the Wii link; waiting for the controller to come back");
    }
}

static void set_cc(uint32_t mask, uint32_t bit, wiimote_button_t button) { wiimote_set_button(button, (mask & bit) != 0); }

void WiiTarget::update(const GamepadState &state)
{
    const uint32_t cc = mapper_.map(state);
    set_cc(cc, kCcUp, WIIMOTE_BUTTON_CLASSIC_UP);
    set_cc(cc, kCcDown, WIIMOTE_BUTTON_CLASSIC_DOWN);
    set_cc(cc, kCcLeft, WIIMOTE_BUTTON_CLASSIC_LEFT);
    set_cc(cc, kCcRight, WIIMOTE_BUTTON_CLASSIC_RIGHT);
    set_cc(cc, kCcA, WIIMOTE_BUTTON_CLASSIC_A);
    set_cc(cc, kCcB, WIIMOTE_BUTTON_CLASSIC_B);
    set_cc(cc, kCcX, WIIMOTE_BUTTON_CLASSIC_X);
    set_cc(cc, kCcY, WIIMOTE_BUTTON_CLASSIC_Y);
    set_cc(cc, kCcPlus, WIIMOTE_BUTTON_CLASSIC_PLUS);
    set_cc(cc, kCcMinus, WIIMOTE_BUTTON_CLASSIC_MINUS);
    set_cc(cc, kCcHome, WIIMOTE_BUTTON_CLASSIC_HOME);
    set_cc(cc, kCcZL, WIIMOTE_BUTTON_CLASSIC_ZL);
    set_cc(cc, kCcZR, WIIMOTE_BUTTON_CLASSIC_ZR);
    wiimote_set_button(WIIMOTE_BUTTON_CLASSIC_L, state.l2 >= kTriggerClick);
    wiimote_set_button(WIIMOTE_BUTTON_CLASSIC_R, state.r2 >= kTriggerClick);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_LT, state.l2);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_RT, state.r2);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_LX, state.lx);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_LY, state.ly);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_RX, state.rx);
    wiimote_set_axis(WIIMOTE_AXIS_CLASSIC_RY, state.ry);

    if (mirror_core_) {
        // Screens that only listen to the remote ("Press A", Wii Menu) work too.
        set_cc(cc, kCcUp, WIIMOTE_BUTTON_UP);
        set_cc(cc, kCcDown, WIIMOTE_BUTTON_DOWN);
        set_cc(cc, kCcLeft, WIIMOTE_BUTTON_LEFT);
        set_cc(cc, kCcRight, WIIMOTE_BUTTON_RIGHT);
        set_cc(cc, kCcA, WIIMOTE_BUTTON_A);
        set_cc(cc, kCcB, WIIMOTE_BUTTON_B);
        set_cc(cc, kCcY, WIIMOTE_BUTTON_ONE);
        set_cc(cc, kCcX, WIIMOTE_BUTTON_TWO);
        set_cc(cc, kCcPlus, WIIMOTE_BUTTON_PLUS);
        set_cc(cc, kCcMinus, WIIMOTE_BUTTON_MINUS);
    }
    // HOME on the remote always works, even in screens that ignore the CC.
    set_cc(cc, kCcHome, WIIMOTE_BUTTON_HOME);

    if (state.battery != battery_sent_ && state.battery >= 0) {
        // The Wii warns below 0x20, so only an empty controller reports low.
        battery_sent_ = state.battery;
        wiimote_set_battery(state.battery == 0 ? 0x10 : static_cast<uint8_t>(0x20 + state.battery * 0xa8 / 100));
    }

    // Send this report now instead of at the next 5 ms flush.
    wiimote_send_now();

    // A bonded remote reconnects when a button is pressed.
    if (!connected_ && state.any_input()) {
        try_connect("button press");
    }
}

void WiiTarget::release_inputs() { update(GamepadState{}); }

void WiiTarget::tick(int64_t now_ms)
{
    now_ = now_ms;
    if (connected_ || !input_present_) {
        return;
    }
    if (repage_at_ms_ >= 0 && now_ >= repage_at_ms_) {
        repage_at_ms_ = -1;
        last_connect_ms_ = now_ - kReconnectRetryMs;
        try_connect("as master");
    } else if (auto_reconnect_until_ms_ >= 0) {
        if (now_ >= auto_reconnect_until_ms_) {
            auto_reconnect_until_ms_ = -1;
            ESP_LOGI(TAG, "Wii did not come back. Press a controller button to retry.");
        } else if (wiimote_is_connecting()) {
            auto_next_page_ms_ = now_ + kAutoReconnectGapMs;
        } else if (now_ >= auto_next_page_ms_) {
            // Page again as soon as the last page ended: the Wii page-scans
            // again within a second or two of a reload.
            last_connect_ms_ = now_ - kReconnectRetryMs;
            try_connect("auto-reconnect");
        }
    }
}

void WiiTarget::send_feedback()
{
    if (listener_ == nullptr) {
        return;
    }
    Feedback f;
    f.player_leds = leds_;
    f.rumble_left = rumble_ ? 1.0f : 0.0f;
    f.rumble_right = rumble_ ? 0.5f : 0.0f;
    listener_->on_target_feedback(f);
}

void WiiTarget::handle_connected()
{
    connected_ = true;
    if (!input_present_) {
        // The ESP32 radio cannot reach a controller while it is the slave on
        // a Wii link, so never sit on the Wii link without one. The pairing is
        // stored; the Wii is paged again when a controller is ready.
        ESP_LOGI(TAG, "Wii paired. Releasing it until a controller is connected.");
        we_dropped_ = true;
        wiimote_disconnect();
        return;
    }
    if (!wiimote_link_is_master()) {
        // The Wii paged us (sync). As its slave the ESP32 radio starves the
        // controller link, so hang up and page the Wii ourselves: the pager
        // stays master of both links.
        ESP_LOGI(TAG, "Wii connected with the ESP32 as slave; reconnecting as master.");
        we_dropped_ = true;
        repage_at_ms_ = now_ + kRepageDelayMs;
        wiimote_disconnect();
        return;
    }
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    ESP_LOGI(TAG, "Wii connected");
    if (listener_ != nullptr) {
        listener_->on_target_connected();
    }
}

void WiiTarget::handle_disconnected()
{
    const bool was_up = connected_;
    connected_ = false;
    leds_ = 0;
    rumble_ = false;
    send_feedback();

    const uint8_t reason = wiimote_last_disconnect_reason();
    const bool ours = we_dropped_;
    we_dropped_ = false;
    ESP_LOGI(TAG, "Wii disconnected (reason 0x%02x%s)", reason, ours ? ", by us" : "");
    if (was_up && listener_ != nullptr) {
        listener_->on_target_disconnected();
    }
    if (ours || !input_present_) {
        return;
    }
    // The Wii hung up while still on (a game or homebrew app starting, the
    // HOME menu's "reconnect", radio trouble): come back on our own.
    auto_reconnect_until_ms_ =
        now_ + (reason == kReasonPowerOff ? kAutoReconnectPowerOffMs : kAutoReconnectWindowMs);
    auto_next_page_ms_ = now_ + kAutoReconnectFirstMs;
}

void WiiTarget::handle_leds(uint8_t mask)
{
    leds_ = mask;
    ESP_LOGI(TAG, "Wii player LEDs: 0x%x", mask);
    send_feedback();
}

void WiiTarget::handle_rumble(bool on)
{
    if (on == rumble_) {
        return;
    }
    rumble_ = on;
    send_feedback();
}

void WiiTarget::handle_sync(bool active)
{
    syncing_ = active;
    // Also fires right before `connected` when the Wii paired, so ask the
    // library rather than our own flag.
    if (!active && !wiimote_is_connected()) {
        ESP_LOGI(TAG, "Sync window closed without a Wii. Try again from the controller.");
    }
    if (listener_ != nullptr) {
        listener_->on_target_sync_changed(active);
    }
}

}  // namespace yucca
