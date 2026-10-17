// Controllers through Bluepad32: DualSense, DualShock 3/4, Switch Pro and
// Joy-Con, Wii Remote, Wii U Pro, 8BitDo, Stadia, Xbox, ... (one at a time).
#pragma once

#include "yucca/input.h"
#include "yucca/target.h"

namespace yucca {

class Bluepad32Input : public InputDriver {
public:
    // Bluepad32 takes C callbacks, so there is one instance.
    static Bluepad32Input &instance();

    // Before begin(): look for BLE controllers too (Stadia, Xbox, some 8BitDo).
    void set_ble_enabled(bool on) { ble_ = on; }

    // Bluepad32's HCI/L2CAP packet handler, for a target that shares the stack.
    static PacketHandler packet_handler();

    // Starts Bluepad32 (uni_init). Call after btstack_init() and the target's begin().
    void begin(InputListener &listener) override;
    bool ready() const override;
    void set_scanning(bool on) override;
    void apply(const Feedback &feedback) override;
    void forget_all() override;
    void tick(int64_t now_ms) override;

private:
    Bluepad32Input() = default;
    bool ble_ = true;
};

}  // namespace yucca
