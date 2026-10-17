// The Switch and Switch 2: YUCCA is a Switch Pro Controller over Bluetooth.
// Protocol: switch_pro. Transport: bt_hid_device. Facts: docs/platforms/switch.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "yucca/bt_hid_device.h"
#include "yucca/mapper.h"
#include "yucca/switch_pro.h"
#include "yucca/target.h"

namespace yucca {

class SwitchTarget : public Target, public bthid::Listener {
public:
    // bt_hid_device is a singleton, so this is too.
    static SwitchTarget &instance();

    enum class Model : uint8_t { Switch, Switch2 };
    // Before begin(). Each console is remembered separately.
    void set_model(Model model) { model_ = model; }
    Model model() const { return model_; }

    const char *id() const override { return model_ == Model::Switch2 ? "switch2" : "switch"; }
    const char *name() const override { return model_ == Model::Switch2 ? "Switch 2" : "Switch"; }

    void set_fallback_handler(PacketHandler handler) override { fallback_ = handler; }
    void begin(TargetListener &listener, Settings &settings) override;

    bool has_paired_console() const override;
    bool connected() const override { return connected_; }
    bool syncing() const override { return syncing_; }

    void start_sync() override;
    void forget() override;

    void on_input_ready(const InputLink &link) override;
    void on_input_gone() override;
    void update(const GamepadState &state) override;
    void tick(int64_t now_ms) override;

    // bthid::Listener
    void on_output(const uint8_t *report, size_t len) override;
    size_t next_input(uint8_t *out, size_t cap, bool periodic) override;
    void on_link(bool up) override;
    void on_sync(bool active) override;

    // Host tests build their own instance.
    SwitchTarget();

private:
    void try_connect(const char *why);
    void send_feedback();
    void hang_up();

    PacketHandler fallback_ = nullptr;
    TargetListener *listener_ = nullptr;
    Model model_ = Model::Switch;
    Mapper mapper_;
    std::optional<switchpro::Protocol> proto_;
    switchpro::State state_;

    // Subcommand replies waiting for the interrupt channel, oldest first.
    static constexpr size_t kReplySlots = 4;
    uint8_t replies_[kReplySlots][switchpro::kReportSize] = {};
    size_t reply_count_ = 0;

    int64_t now_ = 0;
    bool input_present_ = false;
    bool connected_ = false;
    bool syncing_ = false;
    bool we_dropped_ = false;
    int64_t last_connect_ms_;
    int64_t last_report_ms_ = 0;
    int64_t link_up_ms_ = 0;
    int64_t auto_reconnect_until_ms_ = -1;
    int64_t auto_next_page_ms_ = 0;
    int64_t repage_at_ms_ = -1;  // >= 0: slave hand-off in progress
    int64_t paired_ms_ = -1;
    int64_t disconnect_seen_ms_ = -1;
    uint8_t leds_sent_ = 0;
    float rumble_left_sent_ = 0;
    float rumble_right_sent_ = 0;
};

}  // namespace yucca
