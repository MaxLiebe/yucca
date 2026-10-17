// A Bluetooth Classic HID device (the ESP32 as a controller) next to a host
// stack (Bluepad32) on the same BTstack, for consoles that pair with Secure
// Simple Pairing (Switch, Switch 2). The Wii and Wii U use WiimoteAnything,
// which follows the same design with legacy PIN pairing.
//
// This owns L2CAP PSM 0x11/0x13 and hands everything that is not the console
// to the fallback handler (Bluepad32). The console is told apart by being the
// one paired before, or by not being a peripheral while the sync window is
// open. All calls and callbacks run on the BTstack thread.
#pragma once

#include <cstddef>
#include <cstdint>

#include "btstack_defines.h"
#include "bluetooth.h"

namespace yucca::bthid {

struct Profile {
    const char *name;            // local name
    uint32_t cod;                // class of device
    const uint8_t *descriptor;   // HID report descriptor (SDP 0x0206)
    uint16_t descriptor_len;
    const char *service_name;    // SDP 0x0100
    const char *service_description;
    const char *provider;
    uint8_t subclass;            // SDP 0x0202
    uint8_t country;             // SDP 0x0203
    uint16_t pnp_vendor;         // Device ID record (USB vendor source)
    uint16_t pnp_product;
    uint16_t pnp_version;
    uint32_t host_tag;           // TLV tag that remembers the console
    uint16_t report_period_ms;   // periodic input reports while connected
    // The console must stay master of its link: pages allow it to take over
    // (a Switch 2 drops a controller that refuses the role switch).
    bool console_master;
};

class Listener {
public:
    virtual ~Listener() = default;
    // A DATA|OUTPUT report from the console (report id first, no 0xA2).
    virtual void on_output(const uint8_t *report, size_t len) = 0;
    // Next input report to send (report id first, no 0xA1), or 0 for none.
    // periodic: the report timer fired; otherwise only pending replies.
    virtual size_t next_input(uint8_t *out, size_t cap, bool periodic) = 0;
    // HID channels to the console are both open (true) or gone (false).
    virtual void on_link(bool up) = 0;
    // The sync (discoverable) window opened or closed.
    virtual void on_sync(bool active) = 0;
};

constexpr uint32_t kSyncWindowMs = 30000;
constexpr size_t kMaxReport = 400;

void set_fallback_handler(btstack_packet_handler_t handler);
// Before the host stack powers the radio on (before uni_init).
void begin(const Profile &profile, Listener &listener);

// Discoverable and connectable for kSyncWindowMs so the console can pair.
void sync();
bool syncing();
// Page the remembered console and open HID control, then interrupt.
void connect();
bool connecting();
bool connected();
void disconnect();
bool has_host();
// Forget the console and its link key.
void forget();
bool link_is_master();
// HCI reason of the last console disconnect (0 when none).
uint8_t last_disconnect_reason();
// Longest time the radio had no room for an input report (diagnostics).
uint32_t longest_block_ms();
// Send pending replies now instead of at the next report tick.
void flush();
// For a gamepad link: ESP32 master, no sniff, short poll interval.
void tune_link(hci_con_handle_t handle, const bd_addr_t addr);
// Our Bluetooth address (valid once the radio is on).
void local_address(bd_addr_t out);

}  // namespace yucca::bthid
