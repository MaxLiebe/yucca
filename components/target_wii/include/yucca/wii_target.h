// The Wii: YUCCA is a Wii Remote with a Classic Controller plugged in.
#pragma once

#include <cstdint>

#include "yucca/mapper.h"
#include "yucca/target.h"

namespace yucca {

class WiiTarget : public Target {
public:
    // WiimoteAnything is a singleton, so this is too.
    static WiiTarget &instance();

    const char *id() const override { return "wii"; }
    const char *name() const override { return "Wii"; }

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

    // Settings keys (also shown in the web settings page).
    static constexpr const char *kMirrorKey = "wii_mirror";  // int: also press Wii Remote buttons

    // WiimoteAnything callbacks (public for the C trampolines and host tests).
    void handle_connected();
    void handle_disconnected();
    void handle_leds(uint8_t mask);
    void handle_rumble(bool on);
    void handle_sync(bool active);

private:
    WiiTarget();
    void try_connect(const char *why);
    void release_inputs();
    void send_feedback();

    PacketHandler fallback_ = nullptr;
    TargetListener *listener_ = nullptr;
    Mapper mapper_;
    bool mirror_core_ = true;

    int64_t now_ = 0;
    bool input_present_ = false;
    bool connected_ = false;
    bool syncing_ = false;
    bool we_dropped_ = false;
    int64_t last_connect_ms_;
    int64_t auto_reconnect_until_ms_ = -1;
    int64_t auto_next_page_ms_ = 0;
    int64_t repage_at_ms_ = -1;
    int8_t battery_sent_ = -2;
    uint8_t leds_ = 0;
    bool rumble_ = false;
};

}  // namespace yucca
