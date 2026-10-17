#include "yucca/app.h"

#include "esp_log.h"
#include "yucca/settings.h"

namespace yucca {

static const char *TAG = "yucca";

App::App(Settings &settings, InputDriver &input, Target &target)
    : settings_(settings), input_(input), target_(target)
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

    // Select + Start held: sync again (e.g. to pair with another console).
    const uint32_t combo = bit(Button::Select) | bit(Button::Start);
    if (input_.ready() && (buttons_ & combo) == combo) {
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
    target_.on_input_gone();
    update_scanning();
}

void App::on_input_state(const GamepadState &state)
{
    buttons_ = state.buttons;
    target_.update(state);
}

void App::on_target_connected() { ESP_LOGI(TAG, "%s connected", target_.name()); }

void App::on_target_disconnected() { ESP_LOGI(TAG, "%s disconnected", target_.name()); }

void App::on_target_sync_changed(bool) { update_scanning(); }

void App::on_target_feedback(const Feedback &feedback) { input_.apply(feedback); }

}  // namespace yucca
