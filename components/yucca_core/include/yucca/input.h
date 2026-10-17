// The controller side.
#pragma once

#include <cstdint>

#include "yucca/gamepad.h"
#include "yucca/target.h"

namespace yucca {

class InputListener {
public:
    virtual ~InputListener() = default;
    virtual void on_input_ready(const InputLink &link) = 0;
    virtual void on_input_gone() = 0;
    virtual void on_input_state(const GamepadState &state) = 0;
};

class InputDriver {
public:
    virtual ~InputDriver() = default;
    virtual void begin(InputListener &listener) = 0;
    virtual bool ready() const = 0;
    // Look for new controllers (inquiry / BLE scan) or stop looking.
    virtual void set_scanning(bool on) = 0;
    virtual void apply(const Feedback &feedback) = 0;
    // Disconnect and forget every paired controller.
    virtual void forget_all() = 0;
    // Housekeeping, every 20 ms or so (e.g. keep long rumbles going).
    virtual void tick(int64_t now_ms) = 0;
};

}  // namespace yucca
