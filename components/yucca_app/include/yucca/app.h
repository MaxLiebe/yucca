// Wires one input to one target: forwards state and feedback, decides when to
// look for controllers, and handles the sync/forget gestures.
#pragma once

#include <cstdint>

#include "yucca/input.h"
#include "yucca/target.h"

namespace yucca {

class Settings;

class App : public InputListener, public TargetListener {
public:
    App(Settings &settings, InputDriver &input, Target &target);

    // Starts the target, then the input (which starts the Bluetooth host).
    void begin();

    // Every 20 ms. `boot_button` is the BOOT button level (true = held).
    void tick(int64_t now_ms, bool boot_button);

    // InputListener
    void on_input_ready(const InputLink &link) override;
    void on_input_gone() override;
    void on_input_state(const GamepadState &state) override;

    // TargetListener
    void on_target_connected() override;
    void on_target_disconnected() override;
    void on_target_sync_changed(bool syncing) override;
    void on_target_feedback(const Feedback &feedback) override;

    // Gesture timings.
    static constexpr int64_t kSyncComboHoldMs = 3000;  // Select + Start
    static constexpr int64_t kForgetHoldMs = 5000;     // BOOT
    static constexpr int64_t kBootTapMinMs = 50;       // BOOT short press: sync

    void forget_everything();

private:
    void update_scanning();
    void start_sync();

    Settings &settings_;
    InputDriver &input_;
    Target &target_;

    int64_t now_ = 0;
    bool scanning_ = false;
    bool scanning_known_ = false;
    uint32_t buttons_ = 0;
    int64_t combo_since_ms_ = -1;
    bool combo_fired_ = false;
    int64_t boot_down_ms_ = -1;
    bool boot_long_fired_ = false;
};

}  // namespace yucca
