// The Wii: YUCCA is a Wii Remote with a Classic Controller plugged in.
// The Wii U: a Wii U Pro Controller, or (setting) the same Wii Remote +
// Classic Controller for games that do not take the Pro Controller.
#pragma once

#include <cstdint>

#include "yucca/mapper.h"
#include "yucca/target.h"

namespace yucca {

class WiiTarget : public Target {
public:
    // WiimoteAnything is a singleton, so this is too.
    static WiiTarget &instance();

    enum class Model : uint8_t { Wii, WiiU };
    // Before begin(). Default: Wii.
    void set_model(Model model) { model_ = model; }
    Model model() const { return model_; }
    // The Wii U Pro Controller is presented (Wii U, wiiu_mode 0).
    bool pro() const { return pro_; }

    const char *id() const override { return model_ == Model::WiiU ? "wiiu" : "wii"; }
    const char *name() const override { return model_ == Model::WiiU ? "Wii U" : "Wii"; }

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
    static constexpr const char *kWiiuModeKey = "wiiu_mode";  // int: 0 Pro Controller, 1 Wii Remote + Classic
    // Internal: which identity the remembered console was paired as (Identity).
    static constexpr const char *kBondKey = "wii_bond";
    enum Identity : int32_t { kWiiRemote = 0, kWiiuPro = 1, kWiiuRemote = 2 };
    Identity identity() const;

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
    void update_classic(const GamepadState &state);
    void update_pro(const GamepadState &state);

    PacketHandler fallback_ = nullptr;
    TargetListener *listener_ = nullptr;
    Settings *settings_ = nullptr;
    Mapper mapper_;
    Mapper pro_mapper_;
    Model model_ = Model::Wii;
    bool pro_ = false;
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
