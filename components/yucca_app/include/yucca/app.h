// Wires one input to one target: forwards state and feedback, decides when to
// look for controllers, and handles the sync/forget gestures.
#pragma once

#include <atomic>
#include <cstdint>

#include "yucca/input.h"
#include "yucca/settings_schema.h"
#include "yucca/target.h"

namespace yucca {

class Settings;

class App : public InputListener, public TargetListener {
public:
    // `targets` lists the consoles built in; `restart` reboots the ESP32.
    App(Settings &settings, InputDriver &input, Target &target, const SettingChoice *targets, uint8_t target_count,
        void (*restart)());

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
    static constexpr int64_t kMenuComboHoldMs = 3000;  // Select + Home: console menu
    static constexpr int64_t kMenuTimeoutMs = 15000;
    static constexpr int64_t kMenuPulseMs = 150;

    bool menu_open() const { return menu_open_; }

    void forget_everything();

    // Safe from other tasks (the serial console); handled on the next tick.
    void request_sync() { sync_requested_ = true; }
    void request_forget() { forget_requested_ = true; }

private:
    void update_scanning();
    void start_sync();
    void open_menu();
    void close_menu();
    void menu_input(const GamepadState &state);
    void show_menu(bool pulse);

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

    std::atomic<bool> sync_requested_{false};
    std::atomic<bool> forget_requested_{false};
    const SettingChoice *targets_;
    uint8_t target_count_;
    void (*restart_)();
    Feedback target_feedback_;
    int64_t menu_combo_since_ms_ = -1;
    bool menu_open_ = false;
    bool menu_armed_ = false;  // all buttons released since the menu opened
    uint8_t menu_index_ = 0;
    uint32_t menu_prev_buttons_ = 0;
    int64_t menu_deadline_ms_ = 0;
    int64_t menu_pulse_until_ms_ = -1;
};

}  // namespace yucca
