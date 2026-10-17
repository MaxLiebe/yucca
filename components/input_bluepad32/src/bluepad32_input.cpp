#include "yucca/bluepad32_input.h"

#include <cstring>

#include <uni.h>

#include "convert.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace yucca {

static_assert(bp32::kButtonA == int(BUTTON_A) && bp32::kButtonB == int(BUTTON_B) && bp32::kButtonX == int(BUTTON_X) &&
                  bp32::kButtonY == int(BUTTON_Y) && bp32::kShoulderL == int(BUTTON_SHOULDER_L) &&
                  bp32::kShoulderR == int(BUTTON_SHOULDER_R) && bp32::kTriggerL == int(BUTTON_TRIGGER_L) &&
                  bp32::kTriggerR == int(BUTTON_TRIGGER_R) && bp32::kThumbL == int(BUTTON_THUMB_L) &&
                  bp32::kThumbR == int(BUTTON_THUMB_R),
              "Bluepad32 button bits changed");
static_assert(bp32::kMiscSystem == int(MISC_BUTTON_SYSTEM) && bp32::kMiscSelect == int(MISC_BUTTON_SELECT) &&
                  bp32::kMiscStart == int(MISC_BUTTON_START) && bp32::kMiscCapture == int(MISC_BUTTON_CAPTURE),
              "Bluepad32 misc button bits changed");
static_assert(bp32::kDpadUp == int(DPAD_UP) && bp32::kDpadDown == int(DPAD_DOWN) && bp32::kDpadRight == int(DPAD_RIGHT) &&
                  bp32::kDpadLeft == int(DPAD_LEFT),
              "Bluepad32 d-pad bits changed");

static const char *TAG = "input";

// Long rumbles are asked for in pieces of this length and renewed in time.
static constexpr int kRumbleRefreshMs = 4000;

namespace {
uni_hid_device_t *s_pad;
InputListener *s_listener;
bool s_ble = true;
Feedback s_feedback;
int64_t s_rumble_sent_ms;
// Last report from the pad, to log freezes on the controller's side.
int64_t s_data_ms;
bool s_silent;
constexpr int64_t kSilentLogMs = 300;
// Scanning can only start once Bluepad32 finished its setup.
bool s_init_done;
void (*s_console_hook)();
bool s_want_scan;

void apply_scanning()
{
    if (!s_init_done) {
        return;
    }
    if (s_want_scan) {
        ESP_LOGI(TAG, "Looking for a controller (put it in pairing mode, or press its PS/Home button)");
        uni_bt_start_scanning_and_autoconnect_unsafe();
    } else {
        uni_bt_stop_scanning_unsafe();
    }
}

int64_t now_ms() { return esp_timer_get_time() / 1000; }

void pad_set_leds(uint8_t mask)
{
    if (s_pad == nullptr) {
        return;
    }
    if (s_pad->report_parser.set_player_leds != nullptr) {
        s_pad->report_parser.set_player_leds(s_pad, mask & 0x0f);
    } else if (s_pad->report_parser.set_lightbar_color != nullptr) {
        // DualShock 4 and friends: a colour per player (dim when none).
        static const uint8_t kColors[5][3] = {
            {0x08, 0x08, 0x08}, {0x00, 0x00, 0xff}, {0xff, 0x00, 0x00}, {0x00, 0xff, 0x00}, {0xff, 0x00, 0xff},
        };
        int player = 0;
        for (int i = 0; i < 4; i++) {
            if (mask & (1u << i)) {
                player = i + 1;
                break;
            }
        }
        s_pad->report_parser.set_lightbar_color(s_pad, kColors[player][0], kColors[player][1], kColors[player][2]);
    }
}

bool rumbling(const Feedback &f) { return f.rumble_left > 0 || f.rumble_right > 0; }

void pad_rumble(const Feedback &f)
{
    s_rumble_sent_ms = now_ms();
    if (s_pad == nullptr || s_pad->report_parser.play_dual_rumble == nullptr) {
        return;
    }
    if (rumbling(f)) {
        // Ask for longer than the refresh period, so it never gaps.
        s_pad->report_parser.play_dual_rumble(s_pad, 0, kRumbleRefreshMs + 1000,
                                              static_cast<uint8_t>(f.rumble_right * 255 + 0.5f),
                                              static_cast<uint8_t>(f.rumble_left * 255 + 0.5f));
    } else {
        s_pad->report_parser.play_dual_rumble(s_pad, 0, 0, 0, 0);
    }
}

void platform_init(int, const char **)
{
    // Runs after Bluepad32 loaded its properties, before Bluetooth setup.
    uni_bt_le_set_enabled(s_ble);
}

void platform_on_init_complete(void)
{
    // One physical DualSense = one slot (no virtual touchpad mouse).
    uni_virtual_device_set_enabled(false);
    s_init_done = true;
    apply_scanning();
}

uni_error_t platform_on_device_discovered(bd_addr_t, const char *name, uint16_t cod, uint8_t)
{
    if (s_pad != nullptr) {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    // Keyboards and mice are peripherals too; YUCCA wants controllers.
    if ((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD_AND_MICE) {
        ESP_LOGI(TAG, "Ignoring keyboard/mouse %s", name ? name : "");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

void platform_on_device_connected(uni_hid_device_t *d)
{
    ESP_LOGI(TAG, "Gamepad connecting: %s", bd_addr_to_str(d->conn.btaddr));
}

void platform_on_device_disconnected(uni_hid_device_t *d)
{
    if (d != s_pad) {
        return;
    }
    ESP_LOGI(TAG, "Gamepad disconnected");
    s_pad = nullptr;
    if (s_listener != nullptr) {
        s_listener->on_input_gone();
    }
}

uni_error_t platform_on_device_ready(uni_hid_device_t *d)
{
    if (s_pad != nullptr && s_pad != d) {
        ESP_LOGI(TAG, "Already have a gamepad; rejecting %s", bd_addr_to_str(d->conn.btaddr));
        return UNI_ERROR_IGNORE_DEVICE;
    }
    s_pad = d;
    ESP_LOGI(TAG, "Gamepad ready: %s, %s (%s, %s)", d->name, uni_gamepad_get_model_name(d->controller_type),
             bd_addr_to_str(d->conn.btaddr), d->conn.protocol == UNI_BT_CONN_PROTOCOL_BLE ? "BLE" : "Classic");
    InputLink link;
    link.classic = d->conn.protocol == UNI_BT_CONN_PROTOCOL_BR_EDR;
    link.handle = d->conn.handle;
    std::memcpy(link.addr, d->conn.btaddr, sizeof(link.addr));
    // The listener tunes the link first; output reports come after.
    if (s_listener != nullptr) {
        s_listener->on_input_ready(link);
    }
    pad_set_leds(s_feedback.player_leds);
    if (rumbling(s_feedback)) {
        pad_rumble(s_feedback);
    }
    return UNI_ERROR_SUCCESS;
}

void platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
    if (d != s_pad || ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD || s_listener == nullptr) {
        return;
    }
    const int64_t now = now_ms();
    if (s_silent) {
        s_silent = false;
        ESP_LOGW(TAG, "Controller reports again after %d ms", int(now - s_data_ms));
    }
    s_data_ms = now;
    const uni_gamepad_t &gp = ctl->gamepad;
    bp32::RawPad raw;
    raw.dpad = gp.dpad;
    raw.axis_x = gp.axis_x;
    raw.axis_y = gp.axis_y;
    raw.axis_rx = gp.axis_rx;
    raw.axis_ry = gp.axis_ry;
    raw.brake = gp.brake;
    raw.throttle = gp.throttle;
    raw.buttons = gp.buttons;
    raw.misc_buttons = gp.misc_buttons;
    for (int i = 0; i < 3; i++) {
        raw.gyro[i] = gp.gyro[i];
        raw.accel[i] = gp.accel[i];
    }
    raw.battery = ctl->battery;
    s_listener->on_input_state(bp32::convert(raw));
}

const uni_property_t *platform_get_property(uni_property_idx_t) { return nullptr; }

void platform_on_oob_event(uni_platform_oob_event_t, void *) {}

void platform_register_console_cmds(void)
{
    if (s_console_hook != nullptr) {
        s_console_hook();
    }
}

struct uni_platform s_platform = {
    .name = "YUCCA",
    .init = platform_init,
    .on_init_complete = platform_on_init_complete,
    .on_device_discovered = platform_on_device_discovered,
    .on_device_connected = platform_on_device_connected,
    .on_device_disconnected = platform_on_device_disconnected,
    .on_device_ready = platform_on_device_ready,
    .on_gamepad_data = nullptr,
    .on_controller_data = platform_on_controller_data,
    .get_property = platform_get_property,
    .on_oob_event = platform_on_oob_event,
    .device_dump = nullptr,
    .register_console_cmds = platform_register_console_cmds,
};
}  // namespace

Bluepad32Input &Bluepad32Input::instance()
{
    static Bluepad32Input input;
    return input;
}

PacketHandler Bluepad32Input::packet_handler() { return uni_bt_packet_handler; }

void Bluepad32Input::begin(InputListener &listener)
{
    s_listener = &listener;
    s_ble = ble_;
    s_console_hook = console_hook_;
    uni_platform_set_custom(&s_platform);
    uni_init(0, nullptr);
}

bool Bluepad32Input::ready() const { return s_pad != nullptr; }

void Bluepad32Input::set_scanning(bool on)
{
    s_want_scan = on;
    apply_scanning();
}

void Bluepad32Input::apply(const Feedback &feedback)
{
    if (feedback.player_leds != s_feedback.player_leds) {
        pad_set_leds(feedback.player_leds);
    }
    const bool rumble_changed =
        feedback.rumble_left != s_feedback.rumble_left || feedback.rumble_right != s_feedback.rumble_right;
    s_feedback = feedback;
    if (rumble_changed) {
        pad_rumble(feedback);
    }
}

void Bluepad32Input::forget_all()
{
    if (s_pad != nullptr) {
        uni_hid_device_t *pad = s_pad;
        s_pad = nullptr;
        uni_hid_device_disconnect(pad);
        uni_hid_device_delete(pad);
        // The disconnect callback no longer matches s_pad, so report it here.
        if (s_listener != nullptr) {
            s_listener->on_input_gone();
        }
    }
    uni_bt_del_keys_unsafe();
}

void Bluepad32Input::tick(int64_t now)
{
    if (s_pad != nullptr && s_data_ms != 0 && !s_silent && now - s_data_ms >= kSilentLogMs) {
        s_silent = true;
        ESP_LOGW(TAG, "Controller sent no report for %d ms", int(kSilentLogMs));
    }
    if (rumbling(s_feedback) && now - s_rumble_sent_ms >= kRumbleRefreshMs) {
        pad_rumble(s_feedback);
    }
}

}  // namespace yucca
