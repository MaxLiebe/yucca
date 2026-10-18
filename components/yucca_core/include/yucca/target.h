// A console-side device YUCCA emulates (a Wii Remote, a Switch Pro Controller, ...).
#pragma once

#include <cstdint>

#include "yucca/gamepad.h"

namespace yucca {

class Settings;

// Implemented by the app; targets report through it.
class TargetListener {
public:
    virtual ~TargetListener() = default;
    virtual void on_target_connected() = 0;
    virtual void on_target_disconnected() = 0;
    virtual void on_target_sync_changed(bool syncing) = 0;
    virtual void on_target_feedback(const Feedback &feedback) = 0;
};

// The controller link, as far as a target needs to know it.
struct InputLink {
    bool classic = false;  // BR/EDR (true) or BLE
    uint16_t handle = 0;   // HCI connection handle (classic only)
    uint8_t addr[6] = {};
};

// BTstack's packet handler signature, without pulling in BTstack headers.
using PacketHandler = void (*)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

class Target {
public:
    virtual ~Target() = default;
    virtual const char *id() const = 0;    // settings value, e.g. "wii"
    virtual const char *name() const = 0;  // for logs, e.g. "Wii"

    // Bluetooth traffic that is not for the console (the controller's HID
    // channels, its pairing events) goes here. Set before begin().
    virtual void set_fallback_handler(PacketHandler handler) = 0;

    // Called once, after the Bluetooth stack is up and before the input starts.
    virtual void begin(TargetListener &listener, Settings &settings) = 0;

    virtual bool has_paired_console() const = 0;
    virtual bool connected() const = 0;
    virtual bool syncing() const = 0;
    // Whether the app may look for new controllers now: an inquiry takes the
    // radio from the console link for seconds.
    virtual bool may_scan() const { return !syncing() && !connected(); }

    virtual void start_sync() = 0;
    virtual void forget() = 0;

    // A controller became ready (with its link), or went away.
    virtual void on_input_ready(const InputLink &link) = 0;
    virtual void on_input_gone() = 0;

    // New controller state. Called on every report from the controller.
    virtual void update(const GamepadState &state) = 0;

    // Housekeeping, every 20 ms or so.
    virtual void tick(int64_t now_ms) = 0;
};

}  // namespace yucca
