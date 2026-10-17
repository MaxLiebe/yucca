// A scriptable stand-in for WiimoteAnything, for WiiTarget host tests.
#pragma once

#include <cstdint>

#include "wiimote.h"

struct FakeWiimote {
    const wiimote_callbacks_t *callbacks = nullptr;
    bool paired = false;
    bool connecting = false;
    bool connected = false;
    bool master = true;
    uint8_t reason = 0;
    int sync_calls = 0;
    int connect_calls = 0;
    int disconnect_calls = 0;
    int forget_calls = 0;
    int tune_calls = 0;
    int send_calls = 0;
    bool buttons[WIIMOTE_BUTTON_COUNT] = {};
    float axes[WIIMOTE_AXIS_COUNT] = {};
    uint8_t battery = 0;
};

extern FakeWiimote g_wm;
void fake_reset();
