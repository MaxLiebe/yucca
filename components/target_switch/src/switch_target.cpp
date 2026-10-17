#include "yucca/switch_target.h"

#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "yucca/settings.h"

namespace yucca {

static const char *TAG = "switch";

namespace sp = switchpro;

// Button presses retry a failed page at most this often.
static constexpr int64_t kReconnectRetryMs = 1000;
// After the console dropped us without asking (radio trouble, a system
// update), page it again back to back for this long. Reason 0x15 ("power
// off") gets a shorter window.
static constexpr int64_t kAutoReconnectWindowMs = 60000;
static constexpr int64_t kAutoReconnectPowerOffMs = 15000;
static constexpr int64_t kAutoReconnectFirstMs = 200;
static constexpr int64_t kAutoReconnectGapMs = 100;
// Re-page this soon after dropping a link on which the ESP32 is the slave.
static constexpr int64_t kRepageDelayMs = 1000;
// On a slave link (the console paged us to pair), stay until the console has
// set the player lights and this long after, so it finishes the handshake
// (38/48/40 can follow) and the L + R grip confirmation...
static constexpr int64_t kPairedSettleMs = 2000;
// ...or this long without lights: the grip screen is slow.
static constexpr int64_t kSlaveGraceMs = 15000;
// After 06 00, wait at most this long for the ACK to leave before hanging up.
static constexpr int64_t kAckWaitMs = 200;
// Before the player lights are set the console is busy with the handshake;
// flooding it makes pairing fail, so full reports go out at most this often.
static constexpr int64_t kHandshakeReportMs = 60;
// Until the console sends its first output report, a neutral 0x3F report
// this often. A Switch 2 that paged us sent nothing for 15 s without it;
// joycontrol sends empty reports for the same reason.
static constexpr int64_t kHelloMs = 1000;
static constexpr uint8_t kReasonPowerOff = 0x15;
// Digital ZL/ZR from analog triggers.
static constexpr float kTriggerPress = 0.5f;
// Rumble changes smaller than this are not passed to the controller.
static constexpr float kRumbleStep = 0.05f;
// Rumble changes reach the controller at most this often (tick sends the
// latest). Games retune and pulse rumble every frame, and each change is an
// output report to the pad that competes with the console link for the radio:
// at frame rate, Bluepad32's output queue to a DualSense overflowed.
static constexpr int64_t kRumbleGapMs = 100;

// Nintendo layout: A east, B south, X north, Y west. L1/R1 are L/R; ZL/ZR
// come from the triggers below.
static const ButtonRoute kRoutes[] = {
    {Button::DpadUp, sp::kUp},       {Button::DpadDown, sp::kDown}, {Button::DpadLeft, sp::kLeft},
    {Button::DpadRight, sp::kRight}, {Button::East, sp::kA},        {Button::South, sp::kB},
    {Button::North, sp::kX},         {Button::West, sp::kY},        {Button::Start, sp::kPlus},
    {Button::Select, sp::kMinus},    {Button::Home, sp::kHome},     {Button::Capture, sp::kCapture},
    {Button::L1, sp::kL},            {Button::R1, sp::kR},          {Button::L2, sp::kZL},
    {Button::R2, sp::kZR},           {Button::L3, sp::kLStick},     {Button::R3, sp::kRStick},
};

// "Wireless Gamepad" HID descriptor, Nintendo's, as published by nxbt (MIT).
// docs/platforms/switch.md 2.3.
static const uint8_t kDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x06, 0x01, 0xFF, 0x85, 0x21, 0x09, 0x21, 0x75, 0x08, 0x95,
    0x30, 0x81, 0x02, 0x85, 0x30, 0x09, 0x30, 0x75, 0x08, 0x95, 0x30, 0x81, 0x02, 0x85, 0x31, 0x09,
    0x31, 0x75, 0x08, 0x96, 0x69, 0x01, 0x81, 0x02, 0x85, 0x32, 0x09, 0x32, 0x75, 0x08, 0x96, 0x69,
    0x01, 0x81, 0x02, 0x85, 0x33, 0x09, 0x33, 0x75, 0x08, 0x96, 0x69, 0x01, 0x81, 0x02, 0x85, 0x3F,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x05, 0x09,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,
    0x16, 0x00, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0x06, 0x01,
    0xFF, 0x85, 0x01, 0x09, 0x01, 0x75, 0x08, 0x95, 0x30, 0x91, 0x02, 0x85, 0x10, 0x09, 0x10, 0x75,
    0x08, 0x95, 0x30, 0x91, 0x02, 0x85, 0x11, 0x09, 0x11, 0x75, 0x08, 0x95, 0x30, 0x91, 0x02, 0x85,
    0x12, 0x09, 0x12, 0x75, 0x08, 0x95, 0x30, 0x91, 0x02, 0xC0,
};
static_assert(sizeof(kDescriptor) == 170, "Switch HID descriptor is 170 bytes");

// TLV tags that remember each console ("SWCH", "SW2H").
static constexpr uint32_t kHostTagSwitch = 0x53574348;
static constexpr uint32_t kHostTagSwitch2 = 0x53573248;

static bthid::Profile make_profile(SwitchTarget::Model model)
{
    bthid::Profile p = {};
    p.name = "Pro Controller";
    p.cod = 0x002508;
    p.descriptor = kDescriptor;
    p.descriptor_len = sizeof(kDescriptor);
    p.service_name = "Wireless Gamepad";
    p.service_description = "Gamepad";
    p.provider = "Nintendo";
    p.subclass = 0x08;
    p.country = 0x21;
    p.pnp_vendor = 0x057E;
    p.pnp_product = 0x2009;
    p.pnp_version = 0x0001;
    p.host_tag = model == SwitchTarget::Model::Switch2 ? kHostTagSwitch2 : kHostTagSwitch;
    p.report_period_ms = 15;
    // A Switch 2 refused every page that kept the ESP32 master (reason 0x13).
    p.console_master = model == SwitchTarget::Model::Switch2;
    return p;
}

SwitchTarget &SwitchTarget::instance()
{
    static SwitchTarget target;
    return target;
}

SwitchTarget::SwitchTarget()
    : mapper_(kRoutes, sizeof(kRoutes) / sizeof(kRoutes[0])), last_connect_ms_(-kReconnectRetryMs)
{
}

void SwitchTarget::begin(TargetListener &listener, Settings &settings)
{
    listener_ = &listener;
    mapper_.set_layout(static_cast<FaceLayout>(settings.get_int(keys::kFaceLayout, 0)));

    // The profile must outlive the transport.
    static bthid::Profile profile;
    profile = make_profile(model_);
    bthid::set_fallback_handler(fallback_);
    bthid::begin(profile, *this);

    if (bthid::has_host()) {
        ESP_LOGI(TAG, "Ready. Turn your controller on; YUCCA then connects to the %s.", name());
    } else {
        ESP_LOGI(TAG, "First run: pair the controller now; then open Change Grip/Order on the %s.", name());
    }
}

bool SwitchTarget::has_paired_console() const { return bthid::has_host(); }

void SwitchTarget::start_sync()
{
    ESP_LOGI(TAG, "Sync: open Controllers > Change Grip/Order on the %s now (%u s)", name(),
             unsigned(bthid::kSyncWindowMs / 1000));
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_) {
        we_dropped_ = true;
        bthid::disconnect();
    }
    bthid::sync();
}

void SwitchTarget::forget()
{
    ESP_LOGW(TAG, "Forgetting the %s", name());
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_) {
        we_dropped_ = true;
    }
    bthid::forget();
}

void SwitchTarget::try_connect(const char *why)
{
    if (connected_ || syncing_ || !bthid::has_host() || bthid::connecting()) {
        return;
    }
    if (!input_present_) {
        // Paging the console without a controller would only waste the radio.
        return;
    }
    if (now_ - last_connect_ms_ < kReconnectRetryMs) {
        return;
    }
    last_connect_ms_ = now_;
    ESP_LOGI(TAG, "Connecting to the %s (%s)", name(), why);
    bthid::connect();
}

void SwitchTarget::hang_up()
{
    we_dropped_ = true;
    bthid::disconnect();
}

void SwitchTarget::on_input_ready(const InputLink &link)
{
    input_present_ = true;
    if (link.classic) {
        bthid::tune_link(link.handle, link.addr);
    }
    last_connect_ms_ = now_ - kReconnectRetryMs;  // allow an immediate attempt
    if (!bthid::has_host() && !syncing_) {
        ESP_LOGI(TAG, "No %s paired yet: opening the sync window.", name());
        start_sync();
    } else {
        try_connect("controller ready");
    }
}

void SwitchTarget::on_input_gone()
{
    input_present_ = false;
    state_.buttons = 0;
    state_.lx = state_.ly = state_.rx = state_.ry = sp::kStickCenter;
    if (proto_) {
        proto_->set_state(state_);
    }
    auto_reconnect_until_ms_ = -1;
    repage_at_ms_ = -1;
    if (connected_ && !bthid::link_is_master()) {
        // As the console's slave the radio starves the controller link, so
        // it could not come back. As master, keep the link: the console
        // simply sees an idle controller until it returns.
        ESP_LOGI(TAG, "Releasing the %s until the controller is back", name());
        hang_up();
    } else if (connected_) {
        ESP_LOGI(TAG, "Keeping the %s link; waiting for the controller to come back", name());
    }
}

static uint8_t battery_level(int8_t percent)
{
    // Pro levels: 8 full, 6 medium, 4 low, 2 critical, 0 empty.
    if (percent < 0) {
        return 8;
    }
    if (percent >= 70) {
        return 8;
    }
    if (percent >= 40) {
        return 6;
    }
    if (percent >= 15) {
        return 4;
    }
    return percent > 0 ? 2 : 0;
}

void SwitchTarget::update(const GamepadState &state)
{
    uint32_t buttons = mapper_.map(state);
    if (state.l2 >= kTriggerPress) {
        buttons |= sp::kZL;
    }
    if (state.r2 >= kTriggerPress) {
        buttons |= sp::kZR;
    }
    state_.buttons = buttons;
    state_.lx = sp::stick_from_unit(state.lx);
    state_.ly = sp::stick_from_unit(state.ly);
    state_.rx = sp::stick_from_unit(state.rx);
    state_.ry = sp::stick_from_unit(state.ry);
    state_.battery = battery_level(state.battery);
    if (proto_) {
        proto_->set_state(state_);
    }

    // A bonded controller reconnects (and wakes a Switch) on a button press.
    if (!connected_ && state.any_input()) {
        try_connect("button press");
    }
}

void SwitchTarget::tick(int64_t now_ms)
{
    now_ = now_ms;
    if (connected_) {
        send_feedback();  // rumble changes held back by kRumbleGapMs
        if (we_dropped_) {
            // Hanging up; the disconnect event follows.
        } else if (proto_ && proto_->disconnect_requested()) {
            // Subcommand 06 00: the console is going to sleep. Hang up once
            // the ACK is out; a button press pages it again.
            if (disconnect_seen_ms_ < 0) {
                disconnect_seen_ms_ = now_;
            }
            if (reply_count_ > 0) {
                bthid::flush();
            }
            if (reply_count_ == 0 || now_ - disconnect_seen_ms_ >= kAckWaitMs) {
                ESP_LOGI(TAG, "%s asked us to disconnect", name());
                hang_up();
            }
        } else if (proto_ && proto_->paired() && paired_ms_ < 0) {
            paired_ms_ = now_;
        } else if (model_ == Model::Switch && !bthid::link_is_master() &&
                   ((paired_ms_ >= 0 && now_ - paired_ms_ >= kPairedSettleMs) ||
                    now_ - link_up_ms_ >= kSlaveGraceMs)) {
            // The console paged us (pairing). As its slave the ESP32 radio
            // starves the controller link, so hang up once it registered us
            // and page it ourselves: the pager stays master of both links.
            // Not on a Switch 2: it refuses pages from a master, so we stay
            // its slave (bt_hid polls the gamepads less often meanwhile).
            ESP_LOGI(TAG, "%s paired with the ESP32 as slave; reconnecting as master.", name());
            repage_at_ms_ = now_;  // hand-off pending: page again once it is down
            hang_up();
        }
        return;
    }
    if (!input_present_) {
        return;
    }
    if (auto_reconnect_until_ms_ >= 0) {
        if (now_ >= auto_reconnect_until_ms_) {
            auto_reconnect_until_ms_ = -1;
            ESP_LOGI(TAG, "%s did not come back. Press a controller button to retry.", name());
        } else if (bthid::connecting()) {
            auto_next_page_ms_ = now_ + kAutoReconnectGapMs;
        } else if (now_ >= auto_next_page_ms_) {
            last_connect_ms_ = now_ - kReconnectRetryMs;
            try_connect("auto-reconnect");
        }
    }
}

void SwitchTarget::send_feedback()
{
    if (listener_ == nullptr) {
        return;
    }
    const uint8_t leds = proto_ && connected_ ? proto_->player_leds() : 0;
    const float left = proto_ && connected_ ? proto_->rumble_left() : 0.f;
    const float right = proto_ && connected_ ? proto_->rumble_right() : 0.f;
    const bool on_off = ((left == 0.f) != (rumble_left_sent_ == 0.f)) ||
                        ((right == 0.f) != (rumble_right_sent_ == 0.f));
    const bool stronger = std::fabs(left - rumble_left_sent_) >= kRumbleStep ||
                          std::fabs(right - rumble_right_sent_) >= kRumbleStep;
    const bool rumble_changed =
        (on_off || stronger) && (!connected_ || now_ - rumble_sent_ms_ >= kRumbleGapMs);
    if (leds == leds_sent_ && !rumble_changed) {
        return;
    }
    if (rumble_changed) {
        rumble_sent_ms_ = now_;
    }
    leds_sent_ = leds;
    rumble_left_sent_ = left;
    rumble_right_sent_ = right;
    Feedback f;
    f.player_leds = leds;
    f.rumble_left = left;
    f.rumble_right = right;
    listener_->on_target_feedback(f);
}

void SwitchTarget::on_output(const uint8_t *report, size_t len)
{
    if (!proto_) {
        return;
    }
    uint8_t reply[sp::kReportSize];
    if (proto_->handle_output(report, len, reply)) {
        if (reply_count_ == kReplySlots) {
            // The console waits for each reply, so this means the channel is
            // stuck; drop the oldest rather than the newest.
            ESP_LOGW(TAG, "Reply queue full; dropping a reply");
            std::memmove(replies_[0], replies_[1], sizeof(replies_[0]) * (kReplySlots - 1));
            reply_count_--;
        }
        std::memcpy(replies_[reply_count_++], reply, sizeof(reply));
        ESP_LOGI(TAG, "Subcommand 0x%02x", proto_->last_subcommand());
    }
    send_feedback();
}

size_t SwitchTarget::next_input(uint8_t *out, size_t cap, bool periodic)
{
    if (!proto_) {
        return 0;
    }
    if (reply_count_ > 0) {
        if (cap < sp::kReportSize) {
            return 0;
        }
        std::memcpy(out, replies_[0], sp::kReportSize);
        std::memmove(replies_[0], replies_[1], sizeof(replies_[0]) * (kReplySlots - 1));
        reply_count_--;
        return sp::kReportSize;
    }
    if (!periodic || cap < sp::kReportSizeMcu) {
        return 0;
    }
    if (!proto_->heard()) {
        if (now_ - last_report_ms_ < kHelloMs) {
            return 0;
        }
        last_report_ms_ = now_;
        return proto_->build_hello(out);
    }
    if (!proto_->paired() && now_ - last_report_ms_ < kHandshakeReportMs) {
        return 0;
    }
    const size_t n = proto_->build_input(out);
    if (n > 0) {
        last_report_ms_ = now_;
    }
    return n;
}

void SwitchTarget::on_link(bool up)
{
    if (up) {
        if (!proto_) {
            bd_addr_t addr;
            bthid::local_address(addr);
            proto_.emplace(addr);
        }
        proto_->reset();
        proto_->set_state(state_);
        reply_count_ = 0;
        link_up_ms_ = now_;
        paired_ms_ = -1;
        disconnect_seen_ms_ = -1;
        last_report_ms_ = now_ - kHelloMs;
        auto_reconnect_until_ms_ = -1;
        if (!input_present_) {
            // Never sit on the console link without a controller: as slave it
            // would block the controller. The pairing is stored.
            ESP_LOGI(TAG, "%s paired. Releasing it until a controller is connected.", name());
            hang_up();
            return;
        }
        connected_ = true;
        if (bthid::link_is_master()) {
            repage_at_ms_ = -1;
        }
        ESP_LOGI(TAG, "%s connected", name());
        if (listener_ != nullptr) {
            listener_->on_target_connected();
        }
        return;
    }

    const bool was_up = connected_;
    connected_ = false;
    reply_count_ = 0;
    send_feedback();
    const uint8_t reason = bthid::last_disconnect_reason();
    const bool ours = we_dropped_ || (proto_ && proto_->disconnect_requested());
    we_dropped_ = false;
    ESP_LOGI(TAG, "%s disconnected (reason 0x%02x%s)", name(), reason, ours ? ", by us" : "");
    if (was_up && listener_ != nullptr) {
        listener_->on_target_disconnected();
    }
    if (repage_at_ms_ >= 0) {
        // Slave hand-off: page the console as master from now on, with
        // retries (it may still be busy on the grip screen).
        repage_at_ms_ = -1;
        if (input_present_) {
            auto_reconnect_until_ms_ = now_ + kAutoReconnectWindowMs;
            auto_next_page_ms_ = now_ + kRepageDelayMs;
        }
        return;
    }
    if (ours || !input_present_ || !was_up) {
        return;
    }
    auto_reconnect_until_ms_ =
        now_ + (reason == kReasonPowerOff ? kAutoReconnectPowerOffMs : kAutoReconnectWindowMs);
    auto_next_page_ms_ = now_ + kAutoReconnectFirstMs;
}

void SwitchTarget::on_sync(bool active)
{
    syncing_ = active;
    if (!active && !bthid::connected()) {
        ESP_LOGI(TAG, "Sync window closed without a %s. Try again from the controller.", name());
    }
    if (listener_ != nullptr) {
        listener_->on_target_sync_changed(active);
    }
}

}  // namespace yucca
