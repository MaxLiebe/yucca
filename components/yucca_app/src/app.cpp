#include "yucca/app.h"

#include <string>

#include "esp_log.h"
#include "yucca/settings.h"

namespace yucca {

static const char *TAG = "yucca";

App::App(Settings &settings, InputDriver &input, Target &target, const SettingChoice *targets, uint8_t target_count,
         void (*restart)())
    : settings_(settings), input_(input), target_(target), targets_(targets), target_count_(target_count),
      restart_(restart)
{
}

void App::begin()
{
    ESP_LOGI(TAG, "Target: %s", target_.name());
    target_.begin(*this, settings_);
    input_.begin(*this);
    update_scanning();
}

// Inquiry for new controllers only when there is none and the console is not
// syncing. Bonded controllers reconnect by paging us, which needs no inquiry.
void App::update_scanning()
{
    const bool want = !input_.ready() && !target_.syncing();
    if (scanning_known_ && want == scanning_) {
        return;
    }
    scanning_known_ = true;
    scanning_ = want;
    input_.set_scanning(want);
}

void App::start_sync()
{
    if (!target_.syncing()) {
        target_.start_sync();
    }
}

void App::forget_everything()
{
    ESP_LOGW(TAG, "Forgetting the console and all controllers");
    target_.forget();
    input_.forget_all();
    buttons_ = 0;
    combo_since_ms_ = -1;
    update_scanning();
}

void App::tick(int64_t now_ms, bool boot_button)
{
    now_ = now_ms;
    if (forget_requested_.exchange(false)) {
        forget_everything();
    }
    if (sync_requested_.exchange(false)) {
        start_sync();
    }

    // Select + Start held: sync again (e.g. to pair with another console).
    const uint32_t combo = bit(Button::Select) | bit(Button::Start);
    if (!menu_open_ && input_.ready() && (buttons_ & combo) == combo) {
        if (combo_since_ms_ < 0) {
            combo_since_ms_ = now_;
            combo_fired_ = false;
        } else if (!combo_fired_ && !target_.syncing() && now_ - combo_since_ms_ >= kSyncComboHoldMs) {
            combo_fired_ = true;
            start_sync();
        }
    } else {
        combo_since_ms_ = -1;
    }

    // Select + Home held: console menu.
    const uint32_t menu_combo = bit(Button::Select) | bit(Button::Home);
    if (!menu_open_ && input_.ready() && (buttons_ & menu_combo) == menu_combo && target_count_ > 1) {
        if (menu_combo_since_ms_ < 0) {
            menu_combo_since_ms_ = now_;
        } else if (now_ - menu_combo_since_ms_ >= kMenuComboHoldMs) {
            menu_combo_since_ms_ = -1;
            open_menu();
        }
    } else {
        menu_combo_since_ms_ = -1;
    }
    if (menu_open_) {
        if (now_ >= menu_deadline_ms_) {
            ESP_LOGI(TAG, "Console menu timed out");
            close_menu();
        } else if (menu_pulse_until_ms_ >= 0 && now_ >= menu_pulse_until_ms_) {
            menu_pulse_until_ms_ = -1;
            show_menu(false);
        }
    }

    // BOOT: short press syncs, a 5 s hold forgets everything.
    if (boot_button) {
        if (boot_down_ms_ < 0) {
            boot_down_ms_ = now_;
            boot_long_fired_ = false;
        } else if (!boot_long_fired_ && now_ - boot_down_ms_ >= kForgetHoldMs) {
            boot_long_fired_ = true;
            forget_everything();
        }
    } else if (boot_down_ms_ >= 0) {
        if (!boot_long_fired_ && now_ - boot_down_ms_ >= kBootTapMinMs) {
            start_sync();
        }
        boot_down_ms_ = -1;
    }

    target_.tick(now_);
    input_.tick(now_);
}

void App::on_input_ready(const InputLink &link)
{
    update_scanning();
    target_.on_input_ready(link);
}

void App::on_input_gone()
{
    buttons_ = 0;
    combo_since_ms_ = -1;
    menu_combo_since_ms_ = -1;
    if (menu_open_) {
        close_menu();
    }
    target_.on_input_gone();
    update_scanning();
}

void App::on_input_state(const GamepadState &state)
{
    buttons_ = state.buttons;
    if (menu_open_) {
        menu_input(state);
        return;
    }
    // While Select + Home is held for the menu, the console sees neither, so
    // its own Home menu does not open first.
    const uint32_t menu_combo = bit(Button::Select) | bit(Button::Home);
    if (target_count_ > 1 && (state.buttons & menu_combo) == menu_combo) {
        GamepadState held = state;
        held.buttons &= ~menu_combo;
        target_.update(held);
        return;
    }
    target_.update(state);
}

void App::on_target_connected() { ESP_LOGI(TAG, "%s connected", target_.name()); }

void App::on_target_disconnected() { ESP_LOGI(TAG, "%s disconnected", target_.name()); }

void App::on_target_sync_changed(bool) { update_scanning(); }

void App::on_target_feedback(const Feedback &feedback)
{
    target_feedback_ = feedback;
    if (!menu_open_) {
        input_.apply(feedback);
    }
}

// --- Console menu: the player LEDs show which console is selected (1..4 =
// Wii, Wii U, Switch, Switch 2 in table order), d-pad left/right changes it,
// A confirms, Select cancels.

void App::open_menu()
{
    menu_open_ = true;
    menu_armed_ = false;
    menu_index_ = 0;
    const std::string current = settings_.get_str(keys::kTarget, targets_[0].value);
    for (uint8_t i = 0; i < target_count_; i++) {
        if (current == targets_[i].value) {
            menu_index_ = i;
        }
    }
    menu_deadline_ms_ = now_ + kMenuTimeoutMs;
    ESP_LOGI(TAG, "Console menu: d-pad left/right to choose, A to confirm, Select to cancel");
    target_.update(GamepadState{});  // release everything on the console side
    show_menu(true);
}

void App::close_menu()
{
    menu_open_ = false;
    menu_pulse_until_ms_ = -1;
    input_.apply(target_feedback_);
}

void App::show_menu(bool pulse)
{
    Feedback f;
    // One LED per console; past four, a binary pattern.
    f.player_leds = menu_index_ < 4 ? static_cast<uint8_t>(1u << menu_index_) : static_cast<uint8_t>(menu_index_ + 1);
    if (pulse) {
        f.rumble_left = 0.6f;
        f.rumble_right = 0.6f;
        menu_pulse_until_ms_ = now_ + kMenuPulseMs;
    }
    input_.apply(f);
}

void App::menu_input(const GamepadState &state)
{
    if (!menu_armed_) {
        // Wait for Select + Home to be let go before reading presses.
        if (state.buttons == 0) {
            menu_armed_ = true;
        }
        menu_prev_buttons_ = state.buttons;
        return;
    }
    const uint32_t pressed = state.buttons & ~menu_prev_buttons_;
    menu_prev_buttons_ = state.buttons;
    if (pressed == 0) {
        return;
    }
    menu_deadline_ms_ = now_ + kMenuTimeoutMs;
    if (pressed & bit(Button::DpadRight)) {
        menu_index_ = static_cast<uint8_t>((menu_index_ + 1) % target_count_);
        ESP_LOGI(TAG, "Console menu: %s", targets_[menu_index_].label);
        show_menu(true);
    } else if (pressed & bit(Button::DpadLeft)) {
        menu_index_ = static_cast<uint8_t>((menu_index_ + target_count_ - 1) % target_count_);
        ESP_LOGI(TAG, "Console menu: %s", targets_[menu_index_].label);
        show_menu(true);
    } else if (pressed & (bit(Button::South) | bit(Button::East))) {
        const char *chosen = targets_[menu_index_].value;
        if (settings_.get_str(keys::kTarget, targets_[0].value) == chosen) {
            ESP_LOGI(TAG, "Console menu: staying on %s", targets_[menu_index_].label);
            close_menu();
            return;
        }
        ESP_LOGI(TAG, "Switching to %s, restarting", targets_[menu_index_].label);
        settings_.set_str(keys::kTarget, chosen);
        settings_.commit();
        restart_();
    } else if (pressed & bit(Button::Select)) {
        ESP_LOGI(TAG, "Console menu cancelled");
        close_menu();
    }
}

}  // namespace yucca
