// Bluetooth Classic HID device next to a host stack. See bt_hid_device.h.
// Same design as components/wiimote_anything/src/wiimote_bt_btstack.inc
// (read its comments for the why of each radio setting), with SSP pairing
// handled by BTstack instead of a legacy PIN.
#include "yucca/bt_hid_device.h"

#include <cstring>

#include "btstack.h"
#include "btstack_tlv.h"
#include "classic/device_id_server.h"
#include "classic/sdp_server.h"
#include "classic/sdp_util.h"
#include "esp_log.h"

namespace yucca::bthid {

namespace {

const char *TAG = "bt_hid";

constexpr uint16_t kPsmControl = 0x11;
constexpr uint16_t kPsmInterrupt = 0x13;
constexpr uint16_t kMtu = 672;
constexpr uint32_t kCodMajorMask = 0x001f00;
constexpr uint32_t kCodMajorPeripheral = 0x000500;
constexpr uint16_t kPageTimeoutConsole = 0x2000;  // 5.12 s
constexpr uint16_t kPageTimeoutDefault = 0x6000;  // BTstack's default
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kDropTimeoutMs = 5000;
constexpr uint32_t kQosLatencyUs = 1250;
constexpr uint8_t kQosBestEffort = 0x01;
constexpr uint8_t kQosGuaranteed = 0x02;
// While the console is master of its link (pairing), gamepads are polled
// this rarely: a 1.25 ms poll starved the console's link on a Switch 2 for
// 40 s at a time.
constexpr uint32_t kQosRelaxedUs = 10000;
// A Switch 2 (console_master) is our master and keeps its link in sniff;
// its link went silent for seconds whenever the gamepad link beside it was
// active (every log through v0.1.12). So while it is master, gamepads sniff
// too: the radio then serves both links at planned anchors. 12 slots
// (7.5 ms) still delivers 133 reports a second.
constexpr uint16_t kPadSniffSlots = 12;
constexpr uint16_t kPadSniffAttempt = 2;
constexpr uint16_t kPadSniffTimeout = 1;

const Profile *s_profile;
Listener *s_listener;
btstack_packet_handler_t s_fallback;
bool s_setup_done;
bool s_sdp_registered;

uint8_t s_hid_record[700];
// Upper bound of the HID record without the descriptor and the three strings
// (the test checks the real size stays below it).
constexpr size_t kSdpFixedBytes = 240;
uint8_t s_pnp_record[128];

btstack_packet_callback_registration_t s_hci_registration;
btstack_timer_source_t s_report_timer;
bool s_report_timer_active;
btstack_timer_source_t s_sync_timer;
btstack_timer_source_t s_connect_watchdog;
// The console's link key, written to flash only once the console link is
// down: a Switch 2 dropped the link (0x08) after the key was written while it
// was up, both during the handshake (v0.1.4) and 5 s after it (v0.1.5).
bool s_key_pending;
bd_addr_t s_key_addr;
link_key_t s_key;
link_key_type_t s_key_type;

void write_pending_key()
{
    if (!s_key_pending) {
        return;
    }
    s_key_pending = false;
    gap_store_link_key_for_bd_addr(s_key_addr, s_key, s_key_type);
    ESP_LOGI(TAG, "Stored the console's link key (type %u)", unsigned(s_key_type));
}

bd_addr_t s_host_addr;
bool s_have_host;

bool s_link_claimed;
bd_addr_t s_link_addr;
hci_con_handle_t s_link_handle = HCI_CON_HANDLE_INVALID;
bool s_link_outgoing;
bool s_link_master;
uint16_t s_cid_control;
uint16_t s_cid_interrupt;
// An outgoing channel abandoned before it opened: its late CHANNEL_OPENED is
// swallowed instead of reaching the host stack.
constexpr int kStaleMax = 4;
uint16_t s_stale_cid[kStaleMax];

void add_stale(uint16_t cid)
{
    if (cid == 0) {
        return;
    }
    for (auto &c : s_stale_cid) {
        if (c == 0) {
            c = cid;
            return;
        }
    }
    s_stale_cid[0] = cid;  // full: the oldest is least likely to still arrive
}

bool s_control_open;
bool s_interrupt_open;
bool s_link_up;
uint8_t s_last_reason;
bool s_sync_active;
uint8_t s_pending_handshake = 0xff;
bool s_restore_role_switch;
bool s_restore_page_timeout;
// drop_link() was called before the link had a handle; it is released once
// the page ends (or the watchdog gives up waiting).
bool s_link_dropping;
// A second console's ACL, hidden from the host stack: disconnected once up.
bd_addr_t s_reject_addr;
bool s_reject_pending;

// Incoming gamepad channels held until the link is as secure as the host
// stack wants (our PSM services are level 0: the console must not be challenged).
constexpr int kHeldMax = 4;
struct Held {
    uint16_t cid;
    hci_con_handle_t handle;
    uint16_t size;
    uint8_t packet[24];
} s_held[kHeldMax];

// Raw HCI commands, sent one at a time.
bool s_cmd_poweroff;
hci_con_handle_t s_poweroff_handle;
bool s_cmd_cancel_page;
bd_addr_t s_cancel_addr;
bool s_cmd_scan_setup;
struct {
    hci_con_handle_t handle;
    uint16_t policy;
} s_policy_q[4];
int s_policy_count;
struct {
    hci_con_handle_t handle;
    uint8_t type;
    uint32_t latency_us;
} s_qos_q[4];
int s_qos_count;
hci_con_handle_t s_qos_inflight = HCI_CON_HANDLE_INVALID;
uint8_t s_qos_inflight_type;
uint32_t s_qos_inflight_latency;
// Gamepad links tuned by tune_link(), and whether their polling is relaxed
// for a console link where the ESP32 is slave.
hci_con_handle_t s_pads[2] = {HCI_CON_HANDLE_INVALID, HCI_CON_HANDLE_INVALID};
bool s_pads_relaxed;
struct SniffCmd {
    hci_con_handle_t handle;
    bool enter;
} s_sniff_q[2];
int s_sniff_count;

void l2cap_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
void connect_watchdog_cb(btstack_timer_source_t *);
void forget_host();

void arm_watchdog(uint32_t ms)
{
    btstack_run_loop_remove_timer(&s_connect_watchdog);
    btstack_run_loop_set_timer_handler(&s_connect_watchdog, connect_watchdog_cb);
    btstack_run_loop_set_timer(&s_connect_watchdog, ms);
    btstack_run_loop_add_timer(&s_connect_watchdog);
}

bool addr_eq(const bd_addr_t a, const bd_addr_t b) { return std::memcmp(a, b, BD_ADDR_LEN) == 0; }

// --- Remembered console

void load_host()
{
    const btstack_tlv_t *tlv = nullptr;
    void *ctx = nullptr;
    uint8_t buf[BD_ADDR_LEN];
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv != nullptr && tlv->get_tag(ctx, s_profile->host_tag, buf, sizeof(buf)) == int(sizeof(buf))) {
        std::memcpy(s_host_addr, buf, BD_ADDR_LEN);
        s_have_host = true;
        ESP_LOGI(TAG, "Paired console: %s", bd_addr_to_str(s_host_addr));
    }
}

void store_host(const bd_addr_t addr)
{
    if (s_have_host && addr_eq(addr, s_host_addr)) {
        return;
    }
    if (s_have_host) {
        // One console at a time: the old one becomes an unknown host again,
        // so it can pair again instead of being taken for a bonded gamepad.
        gap_drop_link_key_for_bd_addr(s_host_addr);
    }
    std::memcpy(s_host_addr, addr, BD_ADDR_LEN);
    s_have_host = true;
    const btstack_tlv_t *tlv = nullptr;
    void *ctx = nullptr;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv != nullptr) {
        tlv->store_tag(ctx, s_profile->host_tag, s_host_addr, BD_ADDR_LEN);
    }
    ESP_LOGI(TAG, "Remembering console %s", bd_addr_to_str(s_host_addr));
}

bool is_link_addr(const bd_addr_t addr) { return s_link_claimed && addr_eq(addr, s_link_addr); }

// The console rather than a gamepad?
bool is_console(const bd_addr_t addr, uint32_t cod, bool cod_known)
{
    if (is_link_addr(addr) || (s_have_host && addr_eq(addr, s_host_addr))) {
        return true;
    }
    // Unknown hosts only while syncing, never something that calls itself a
    // peripheral, and never a device we already have a key for (a bonded
    // gamepad or another host reconnecting).
    if (!cod_known || !s_sync_active || (cod & kCodMajorMask) == kCodMajorPeripheral) {
        return false;
    }
    link_key_t key;
    link_key_type_t type;
    return !gap_get_link_key_for_bd_addr(const_cast<uint8_t *>(addr), key, &type);
}

void claim_link(const bd_addr_t addr, bool outgoing)
{
    s_link_claimed = true;
    std::memcpy(s_link_addr, addr, BD_ADDR_LEN);
    s_link_outgoing = outgoing;
    s_link_master = outgoing;  // the pager starts as master
    s_link_handle = HCI_CON_HANDLE_INVALID;
    s_link_dropping = false;
    s_last_reason = 0;
    // Incoming or outgoing, the HID channels must come up in time.
    arm_watchdog(kConnectTimeoutMs);
}

// --- HCI command queue

void pump()
{
    if (hci_get_state() != HCI_STATE_WORKING || !hci_can_send_command_packet_now()) {
        return;
    }
    if (s_cmd_poweroff) {
        s_cmd_poweroff = false;
        // 0x15: what a controller that is switched off sends.
        hci_send_cmd(&hci_disconnect, s_poweroff_handle,
                     ERROR_CODE_REMOTE_DEVICE_TERMINATED_CONNECTION_DUE_TO_POWER_OFF);
    } else if (s_policy_count > 0) {
        s_policy_count--;
        hci_send_cmd(&hci_write_link_policy_settings, s_policy_q[s_policy_count].handle,
                     s_policy_q[s_policy_count].policy);
    } else if (s_sniff_count > 0) {
        // After the policies: a pad's policy must allow sniff first.
        const SniffCmd c = s_sniff_q[0];
        s_sniff_q[0] = s_sniff_q[1];
        s_sniff_count--;
        ESP_LOGI(TAG, "Gamepad 0x%04x: %s sniff", unsigned(c.handle), c.enter ? "entering" : "leaving");
        if (c.enter) {
            hci_send_cmd(&hci_sniff_mode, c.handle, kPadSniffSlots, kPadSniffSlots, kPadSniffAttempt,
                         kPadSniffTimeout);
        } else {
            hci_send_cmd(&hci_exit_sniff_mode, c.handle);
        }
    } else if (s_qos_count > 0 && s_qos_inflight == HCI_CON_HANDLE_INVALID) {
        s_qos_count--;
        s_qos_inflight = s_qos_q[s_qos_count].handle;
        s_qos_inflight_type = s_qos_q[s_qos_count].type;
        s_qos_inflight_latency = s_qos_q[s_qos_count].latency_us;
        hci_send_cmd(&hci_qos_setup, s_qos_inflight, 0, s_qos_inflight_type, 20000, 0, s_qos_inflight_latency,
                     0xffffffffu);
    } else if (s_cmd_cancel_page) {
        s_cmd_cancel_page = false;
        hci_send_cmd(&hci_create_connection_cancel, s_cancel_addr);
    } else if (s_cmd_scan_setup) {
        s_cmd_scan_setup = false;
        hci_send_cmd(&hci_write_inquiry_scan_type, 1);  // interlaced: found faster
    }
}

void queue_policy(hci_con_handle_t handle, uint16_t policy)
{
    if (s_policy_count < 4) {
        s_policy_q[s_policy_count++] = {handle, policy};
    }
    pump();
}

void queue_qos(hci_con_handle_t handle, uint8_t type, uint32_t latency_us = kQosLatencyUs)
{
    if (s_qos_count < 4) {
        s_qos_q[s_qos_count++] = {handle, type, latency_us};
    }
    pump();
}

// One pending sniff command per pad; a newer one replaces it.
void pad_sniff(hci_con_handle_t handle, bool enter)
{
    if (!s_profile->console_master) {
        return;
    }
    int i = 0;
    while (i < s_sniff_count && s_sniff_q[i].handle != handle) {
        i++;
    }
    if (i == 2) {
        return;
    }
    s_sniff_q[i] = {handle, enter};
    if (i == s_sniff_count) {
        s_sniff_count++;
    }
    pump();
}

// Fast polling for the gamepads, or relaxed while the console is our master.
void tune_pad_qos(hci_con_handle_t handle)
{
    if (s_pads_relaxed) {
        queue_qos(handle, kQosBestEffort, kQosRelaxedUs);
    } else {
        queue_qos(handle, kQosGuaranteed);
    }
}

void set_pads_relaxed(bool relaxed)
{
    if (relaxed == s_pads_relaxed) {
        return;
    }
    s_pads_relaxed = relaxed;
    for (auto h : s_pads) {
        if (h != HCI_CON_HANDLE_INVALID) {
            tune_pad_qos(h);
            pad_sniff(h, relaxed);
        }
    }
}

void qos_failed(hci_con_handle_t handle, uint8_t status)
{
    if (s_qos_inflight_type == kQosGuaranteed) {
        queue_qos(handle, kQosBestEffort);
    } else {
        ESP_LOGW(TAG, "QoS refused on 0x%04x (0x%02x)", handle, status);
    }
}

// --- Reports

void report_timer_cb(btstack_timer_source_t *ts);

void report_timer_start()
{
    if (s_report_timer_active) {
        return;
    }
    s_report_timer_active = true;
    btstack_run_loop_set_timer_handler(&s_report_timer, report_timer_cb);
    btstack_run_loop_set_timer(&s_report_timer, s_profile->report_period_ms);
    btstack_run_loop_add_timer(&s_report_timer);
}

void report_timer_stop()
{
    if (s_report_timer_active) {
        s_report_timer_active = false;
        btstack_run_loop_remove_timer(&s_report_timer);
    }
}

// Send what the listener has. Replies go out as soon as the channel can take
// them; a periodic report only when the timer fired, and is dropped (not
// queued) when the radio is busy, so reports never pile up.
// Periodic reports in a row the radio had no room for. Logged so a hardware
// log shows whether the console link or the controller froze.
uint32_t s_blocked_ticks;
uint32_t s_longest_block_ms;
// Packets the console sent us while our reports were blocked: tells whether
// the console stopped talking too (it stopped polling) or only we can't send.
uint32_t s_rx_in_block;
uint32_t s_last_block_rx;
constexpr uint32_t kBlockLogMs = 300;

void note_blocked()
{
    const uint32_t before = s_blocked_ticks * s_profile->report_period_ms;
    if (s_blocked_ticks == 0) {
        s_rx_in_block = 0;
    }
    s_blocked_ticks++;
    if (before < kBlockLogMs && s_blocked_ticks * s_profile->report_period_ms >= kBlockLogMs) {
        ESP_LOGW(TAG, "Console link blocked: no input report sent for %u ms", unsigned(kBlockLogMs));
    }
}

void note_sent()
{
    const uint32_t ms = s_blocked_ticks * s_profile->report_period_ms;
    s_blocked_ticks = 0;
    if (ms >= kBlockLogMs) {
        s_last_block_rx = s_rx_in_block;
        ESP_LOGW(TAG, "Console link moving again after ~%u ms (console sent %u packets meanwhile)", unsigned(ms),
                 unsigned(s_rx_in_block));
    }
    if (ms > s_longest_block_ms) {
        s_longest_block_ms = ms;
    }
}

void send_reports(bool periodic)
{
    static uint8_t pkt[kMaxReport + 1];
    if (s_pending_handshake != 0xff && s_cid_control != 0) {
        if (l2cap_can_send_packet_now(s_cid_control)) {
            uint8_t hs = s_pending_handshake;
            s_pending_handshake = 0xff;
            l2cap_send(s_cid_control, &hs, 1);
        } else {
            l2cap_request_can_send_now_event(s_cid_control);
        }
    }
    if (!s_link_up || s_cid_interrupt == 0) {
        return;
    }
    for (int budget = 4; budget > 0; budget--) {
        if (!l2cap_can_send_packet_now(s_cid_interrupt)) {
            if (periodic) {
                note_blocked();
            }
            l2cap_request_can_send_now_event(s_cid_interrupt);
            return;
        }
        size_t n = s_listener->next_input(pkt + 1, kMaxReport, periodic);
        if (n == 0) {
            return;
        }
        periodic = false;
        pkt[0] = 0xa1;  // DATA | INPUT
        if (l2cap_send(s_cid_interrupt, pkt, uint16_t(n + 1)) == ERROR_CODE_SUCCESS) {
            note_sent();
        }
    }
}

void report_timer_cb(btstack_timer_source_t *ts)
{
    send_reports(true);
    btstack_run_loop_set_timer(ts, s_profile->report_period_ms);
    btstack_run_loop_add_timer(ts);
}

// --- Link

void restore_page_settings()
{
    if (s_restore_role_switch) {
        s_restore_role_switch = false;
        gap_set_allow_role_switch(true);
    }
    if (s_restore_page_timeout) {
        s_restore_page_timeout = false;
        gap_set_page_timeout(kPageTimeoutDefault);
    }
}

void release_link()
{
    const bool was_up = s_link_up;
    if (s_link_outgoing) {
        add_stale(s_control_open ? 0 : s_cid_control);
        add_stale(s_interrupt_open ? 0 : s_cid_interrupt);
    }
    s_link_claimed = false;
    s_link_outgoing = false;
    s_link_up = false;
    s_link_handle = HCI_CON_HANDLE_INVALID;
    s_link_dropping = false;
    s_cid_control = s_cid_interrupt = 0;
    s_control_open = s_interrupt_open = false;
    s_pending_handshake = 0xff;
    report_timer_stop();
    btstack_run_loop_remove_timer(&s_connect_watchdog);
    restore_page_settings();
    set_pads_relaxed(false);
    write_pending_key();
    if (was_up) {
        ESP_LOGI(TAG, "Console link down");
        s_listener->on_link(false);
    }
}

void end_sync(bool notify)
{
    if (!s_sync_active) {
        return;
    }
    s_sync_active = false;
    btstack_run_loop_remove_timer(&s_sync_timer);
    gap_discoverable_control(0);
    gap_set_page_scan_activity(0x0800, 0x0012);
    gap_inquiry_set_scan_activity(0x0800, 0x0012);
    if (notify) {
        s_listener->on_sync(false);
    }
}

void on_channels_up()
{
    if (s_link_up) {
        return;
    }
    s_link_up = true;
    s_blocked_ticks = 0;
    store_host(s_link_addr);
    end_sync(true);
    btstack_run_loop_remove_timer(&s_connect_watchdog);
    ESP_LOGI(TAG, "Console %s connected (%s, ESP32 %s)", bd_addr_to_str(s_link_addr),
             s_link_outgoing ? "we paged it" : "it paged us", s_link_master ? "master" : "slave");
    report_timer_start();
    s_listener->on_link(true);
}

// A channel to the console without authenticating first: the console starts
// authentication itself, as with a real controller. A Switch 2 instead
// drops a pager that does not authenticate (0x05), so with console_master
// we authenticate and encrypt with the stored key first.
uint8_t create_channel(uint16_t psm, uint16_t *cid)
{
    const gap_security_level_t saved = gap_get_security_level();
    gap_set_security_level(s_profile->console_master ? LEVEL_2 : LEVEL_0);
    const uint8_t status = l2cap_create_channel(l2cap_handler, s_link_addr, psm, kMtu, cid);
    gap_set_security_level(saved);
    return status;
}

void drop_link()
{
    if (!s_link_claimed) {
        return;
    }
    if (s_link_handle != HCI_CON_HANDLE_INVALID) {
        s_poweroff_handle = s_link_handle;
        s_cmd_poweroff = true;
        pump();
        return;  // DISCONNECTION_COMPLETE releases the link
    }
    if (s_link_outgoing) {
        // Still paging: cancel it and keep the claim until the page ends, so
        // a page that completes anyway is ours to hang up.
        if (!s_link_dropping) {
            s_link_dropping = true;
            std::memcpy(s_cancel_addr, s_link_addr, BD_ADDR_LEN);
            s_cmd_cancel_page = true;
            pump();
            arm_watchdog(kDropTimeoutMs);
        }
        return;
    }
    release_link();
}

void connect_watchdog_cb(btstack_timer_source_t *)
{
    if (!s_link_claimed || s_link_up) {
        return;
    }
    if (s_link_dropping) {
        ESP_LOGW(TAG, "Page did not end; releasing the console link");
        release_link();
        return;
    }
    ESP_LOGW(TAG, "Console link did not come up in time");
    drop_link();
}

void sync_timer_cb(btstack_timer_source_t *)
{
    ESP_LOGI(TAG, "Sync window closed");
    end_sync(true);
}

// --- HID data

void handle_hid(uint16_t cid, const uint8_t *data, size_t len)
{
    if (len < 1) {
        return;
    }
    if (cid == s_cid_interrupt) {
        if (data[0] == 0xa2) {  // DATA | OUTPUT
            s_listener->on_output(data + 1, len - 1);
        }
        send_reports(false);
        return;
    }
    const uint8_t type = data[0] >> 4, param = data[0] & 0x0f;
    switch (type) {
    case 0x1:  // HID_CONTROL
        if (param == 0x05) {  // VIRTUAL_CABLE_UNPLUG: the console dropped the bond
            forget_host();
        }
        return;
    case 0x5:  // SET_REPORT (output)
        if ((param & 0x03) == 0x02) {
            s_listener->on_output(data + 1, len - 1);
            s_pending_handshake = 0x00;
        } else {
            s_pending_handshake = 0x03;
        }
        break;
    case 0x7:  // SET_PROTOCOL: accept and ignore
        s_pending_handshake = 0x00;
        break;
    case 0xa:  // DATA on control
        if (param == 0x02) {
            s_listener->on_output(data + 1, len - 1);
        }
        break;
    default:
        s_pending_handshake = 0x03;  // unsupported request
        break;
    }
    send_reports(false);
}

// --- L2CAP

void forward(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (s_fallback != nullptr) {
        s_fallback(packet_type, channel, packet, size);
    }
}

bool hold_until_secure(uint16_t channel, hci_con_handle_t handle, uint8_t *packet, uint16_t size)
{
    const gap_security_level_t need = gap_get_security_level();
    if (need == LEVEL_0 || gap_security_level(handle) >= need || size > sizeof(s_held[0].packet)) {
        return false;
    }
    for (auto &h : s_held) {
        if (h.cid == 0) {
            h.cid = channel;
            h.handle = handle;
            h.size = size;
            std::memcpy(h.packet, packet, size);
            gap_request_security_level(handle, need);
            return true;
        }
    }
    // Never let a gamepad channel through below the host stack's level.
    ESP_LOGW(TAG, "Too many channels waiting for security; declining one");
    l2cap_decline_connection(l2cap_event_incoming_connection_get_local_cid(packet));
    return true;
}

void release_held(hci_con_handle_t handle, int level)
{
    const gap_security_level_t need = gap_get_security_level();
    for (auto &h : s_held) {
        if (h.cid == 0 || h.handle != handle) {
            continue;
        }
        const uint16_t cid = h.cid;
        h.cid = 0;
        if (level < 0) {
            continue;
        }
        if (level >= int(need)) {
            forward(HCI_EVENT_PACKET, cid, h.packet, h.size);
        } else {
            l2cap_decline_connection(cid);
        }
    }
}

bool is_our_cid(uint16_t cid) { return cid != 0 && (cid == s_cid_control || cid == s_cid_interrupt); }

void l2cap_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type == L2CAP_DATA_PACKET) {
        if (is_our_cid(channel)) {
            s_rx_in_block++;
            handle_hid(channel, packet, size);
        } else {
            forward(packet_type, channel, packet, size);
        }
        return;
    }
    if (packet_type != HCI_EVENT_PACKET) {
        forward(packet_type, channel, packet, size);
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case L2CAP_EVENT_INCOMING_CONNECTION: {
        bd_addr_t addr;
        const uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
        const uint16_t cid = l2cap_event_incoming_connection_get_local_cid(packet);
        const hci_con_handle_t handle = l2cap_event_incoming_connection_get_handle(packet);
        l2cap_event_incoming_connection_get_address(packet, addr);
        if (!is_console(addr, 0, false)) {
            if (!hold_until_secure(channel, handle, packet, size)) {
                forward(packet_type, channel, packet, size);
            }
            return;
        }
        if (!s_link_claimed) {
            claim_link(addr, false);
        } else if (!addr_eq(addr, s_link_addr)) {
            l2cap_decline_connection(cid);  // a second console
            return;
        }
        s_link_handle = handle;
        if (psm == kPsmControl && s_cid_control == 0) {
            s_cid_control = cid;
            l2cap_accept_connection(cid);
        } else if (psm == kPsmInterrupt && s_cid_interrupt == 0) {
            s_cid_interrupt = cid;
            l2cap_accept_connection(cid);
        } else {
            l2cap_decline_connection(cid);
        }
        return;
    }
    case L2CAP_EVENT_CHANNEL_OPENED: {
        const uint16_t cid = l2cap_event_channel_opened_get_local_cid(packet);
        for (auto &stale : s_stale_cid) {
            if (cid == 0 || cid != stale) {
                continue;
            }
            stale = 0;
            if (l2cap_event_channel_opened_get_status(packet) == ERROR_CODE_SUCCESS) {
                // A page we gave up on succeeded after all: close the channel,
                // and the link too unless a new attempt to the same console
                // shares it.
                l2cap_disconnect(cid);
                bd_addr_t addr;
                l2cap_event_channel_opened_get_address(packet, addr);
                if (!is_link_addr(addr)) {
                    gap_disconnect(l2cap_event_channel_opened_get_handle(packet));
                }
            }
            return;
        }
        if (!is_our_cid(cid)) {
            forward(packet_type, channel, packet, size);
            return;
        }
        const uint8_t status = l2cap_event_channel_opened_get_status(packet);
        const uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
        if (status != ERROR_CODE_SUCCESS) {
            ESP_LOGW(TAG, "Console channel PSM 0x%04x failed (0x%02x)", psm, status);
            (cid == s_cid_control ? s_cid_control : s_cid_interrupt) = 0;
            if (s_link_handle == HCI_CON_HANDLE_INVALID) {
                release_link();  // the page failed
            } else if (s_link_outgoing) {
                drop_link();
            }
            return;
        }
        s_link_handle = l2cap_event_channel_opened_get_handle(packet);
        (cid == s_cid_control ? s_control_open : s_interrupt_open) = true;
        if (s_link_outgoing && cid == s_cid_control && s_cid_interrupt == 0) {
            if (create_channel(kPsmInterrupt, &s_cid_interrupt) != ERROR_CODE_SUCCESS) {
                s_cid_interrupt = 0;
                drop_link();
            }
        }
        if (s_control_open && s_interrupt_open) {
            on_channels_up();
        }
        return;
    }
    case L2CAP_EVENT_CHANNEL_CLOSED: {
        const uint16_t cid = l2cap_event_channel_closed_get_local_cid(packet);
        if (!is_our_cid(cid)) {
            forward(packet_type, channel, packet, size);
            return;
        }
        if (cid == s_cid_control) {
            s_cid_control = 0;
            s_control_open = false;
        } else {
            s_cid_interrupt = 0;
            s_interrupt_open = false;
        }
        if (s_cid_control == 0 && s_cid_interrupt == 0) {
            if (s_link_handle != HCI_CON_HANDLE_INVALID) {
                gap_disconnect(s_link_handle);  // no HID channels, no link
            } else {
                release_link();
            }
        } else if (s_link_up) {
            s_link_up = false;
            report_timer_stop();
            s_listener->on_link(false);
        }
        return;
    }
    case L2CAP_EVENT_CAN_SEND_NOW:
        if (is_our_cid(l2cap_event_can_send_now_get_local_cid(packet))) {
            send_reports(false);
        } else {
            forward(packet_type, channel, packet, size);
        }
        return;
    default:
        forward(packet_type, channel, packet, size);
        return;
    }
}

void forget_host()
{
    s_key_pending = false;  // never written back after the drop below
    drop_link();
    if (s_have_host) {
        gap_drop_link_key_for_bd_addr(s_host_addr);
    }
    s_have_host = false;
    std::memset(s_host_addr, 0, sizeof(s_host_addr));
    const btstack_tlv_t *tlv = nullptr;
    void *ctx = nullptr;
    btstack_tlv_get_instance(&tlv, &ctx);
    if (tlv != nullptr) {
        tlv->delete_tag(ctx, s_profile->host_tag);
    }
    ESP_LOGI(TAG, "Forgot the console");
}

// --- SDP and identity

void build_hid_record(uint8_t *service, uint32_t handle)
{
    const Profile &p = *s_profile;
    de_create_sequence(service);
    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_RECORD_HANDLE);
    de_add_number(service, DE_UINT, DE_SIZE_32, handle);

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_CLASS_ID_LIST);
    uint8_t *attr = de_push_sequence(service);
    de_add_number(attr, DE_UUID, DE_SIZE_16, BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE);
    de_pop_sequence(service, attr);

    auto protocol_list = [](uint8_t *service, uint16_t attr_id, uint16_t psm) {
        de_add_number(service, DE_UINT, DE_SIZE_16, attr_id);
        uint8_t *attr = de_push_sequence(service);
        uint8_t *seq = attr;
        if (attr_id == BLUETOOTH_ATTRIBUTE_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS) {
            seq = de_push_sequence(attr);
        }
        uint8_t *inner = de_push_sequence(seq);
        de_add_number(inner, DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_L2CAP);
        de_add_number(inner, DE_UINT, DE_SIZE_16, psm);
        de_pop_sequence(seq, inner);
        inner = de_push_sequence(seq);
        de_add_number(inner, DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_HIDP);
        de_pop_sequence(seq, inner);
        if (seq != attr) {
            de_pop_sequence(attr, seq);
        }
        de_pop_sequence(service, attr);
    };
    protocol_list(service, BLUETOOTH_ATTRIBUTE_PROTOCOL_DESCRIPTOR_LIST, kPsmControl);

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BROWSE_GROUP_LIST);
    attr = de_push_sequence(service);
    de_add_number(attr, DE_UUID, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_PUBLIC_BROWSE_ROOT);
    de_pop_sequence(service, attr);

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_LANGUAGE_BASE_ATTRIBUTE_ID_LIST);
    attr = de_push_sequence(service);
    de_add_number(attr, DE_UINT, DE_SIZE_16, 0x656e);  // "en"
    de_add_number(attr, DE_UINT, DE_SIZE_16, 0x006a);  // UTF-8
    de_add_number(attr, DE_UINT, DE_SIZE_16, 0x0100);
    de_pop_sequence(service, attr);

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BLUETOOTH_PROFILE_DESCRIPTOR_LIST);
    attr = de_push_sequence(service);
    uint8_t *seq = de_push_sequence(attr);
    de_add_number(seq, DE_UUID, DE_SIZE_16, BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE);
    de_add_number(seq, DE_UINT, DE_SIZE_16, 0x0101);
    de_pop_sequence(attr, seq);
    de_pop_sequence(service, attr);

    protocol_list(service, BLUETOOTH_ATTRIBUTE_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS, kPsmInterrupt);

    auto str = [service](uint16_t id, const char *s) {
        de_add_number(service, DE_UINT, DE_SIZE_16, id);
        de_add_data(service, DE_STRING, uint16_t(std::strlen(s)), (uint8_t *)s);
    };
    auto u8 = [service](uint16_t id, uint8_t v) {
        de_add_number(service, DE_UINT, DE_SIZE_16, id);
        de_add_number(service, DE_UINT, DE_SIZE_8, v);
    };
    auto u16 = [service](uint16_t id, uint16_t v) {
        de_add_number(service, DE_UINT, DE_SIZE_16, id);
        de_add_number(service, DE_UINT, DE_SIZE_16, v);
    };
    auto flag = [service](uint16_t id, bool v) {
        de_add_number(service, DE_UINT, DE_SIZE_16, id);
        de_add_number(service, DE_BOOL, DE_SIZE_8, v);
    };
    str(0x0100, p.service_name);
    str(0x0101, p.service_description);
    str(0x0102, p.provider);
    u16(BLUETOOTH_ATTRIBUTE_HID_DEVICE_RELEASE_NUMBER, 0x0100);
    u16(BLUETOOTH_ATTRIBUTE_HID_PARSER_VERSION, 0x0111);
    u8(BLUETOOTH_ATTRIBUTE_HID_DEVICE_SUBCLASS, p.subclass);
    u8(BLUETOOTH_ATTRIBUTE_HID_COUNTRY_CODE, p.country);
    flag(BLUETOOTH_ATTRIBUTE_HID_VIRTUAL_CABLE, true);
    flag(BLUETOOTH_ATTRIBUTE_HID_RECONNECT_INITIATE, true);  // the controller reconnects

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_HID_DESCRIPTOR_LIST);
    attr = de_push_sequence(service);
    seq = de_push_sequence(attr);
    de_add_number(seq, DE_UINT, DE_SIZE_8, 0x22);  // report descriptor
    de_add_data(seq, DE_STRING, p.descriptor_len, (uint8_t *)p.descriptor);
    de_pop_sequence(attr, seq);
    de_pop_sequence(service, attr);

    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_HIDLANGID_BASE_LIST);
    attr = de_push_sequence(service);
    seq = de_push_sequence(attr);
    de_add_number(seq, DE_UINT, DE_SIZE_16, 0x0409);
    de_add_number(seq, DE_UINT, DE_SIZE_16, 0x0100);
    de_pop_sequence(attr, seq);
    de_pop_sequence(service, attr);

    flag(BLUETOOTH_ATTRIBUTE_HID_BATTERY_POWER, true);
    flag(BLUETOOTH_ATTRIBUTE_HID_REMOTE_WAKE, true);
    u16(BLUETOOTH_ATTRIBUTE_HID_SUPERVISION_TIMEOUT, 0x0c80);
    flag(BLUETOOTH_ATTRIBUTE_HID_NORMALLY_CONNECTABLE, false);
    flag(BLUETOOTH_ATTRIBUTE_HID_BOOT_DEVICE, false);
}

void register_sdp()
{
    if (s_sdp_registered) {
        return;
    }
    // Bluepad32 registers a Device ID record at 0x10003; a controller has its own.
    if (sdp_get_record_for_handle(0x10003) != nullptr) {
        sdp_unregister_service(0x10003);
    }
    // The builder does not check bounds, so check the variable parts first.
    const size_t need = kSdpFixedBytes + s_profile->descriptor_len + std::strlen(s_profile->service_name) +
                        std::strlen(s_profile->service_description) + std::strlen(s_profile->provider);
    if (need > sizeof(s_hid_record)) {
        ESP_LOGE(TAG, "HID SDP record too large (%u bytes)", unsigned(need));
        return;
    }
    const uint32_t hid = sdp_create_service_record_handle();
    std::memset(s_hid_record, 0, sizeof(s_hid_record));
    build_hid_record(s_hid_record, hid);
    sdp_register_service(s_hid_record);
    const uint32_t pnp = sdp_create_service_record_handle();
    std::memset(s_pnp_record, 0, sizeof(s_pnp_record));
    device_id_create_sdp_record(s_pnp_record, pnp, DEVICE_ID_VENDOR_ID_SOURCE_USB, s_profile->pnp_vendor,
                                s_profile->pnp_product, s_profile->pnp_version);
    sdp_register_service(s_pnp_record);
    s_sdp_registered = true;
}

void apply_identity()
{
    gap_set_local_name(s_profile->name);
    gap_set_class_of_device(s_profile->cod);
    gap_set_allow_role_switch(true);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_connectable_control(1);
}

// Before an incoming ACL is accepted: which role do we ask for?
int connection_filter(bd_addr_t addr, hci_link_type_t link_type)
{
    if (link_type == HCI_LINK_TYPE_ACL) {
        link_key_t key;
        link_key_type_t type;
        const bool known_console = s_have_host && addr_eq(addr, s_host_addr);
        const bool has_key = gap_get_link_key_for_bd_addr(addr, key, &type);
        // The console stays master of a link it opened; gamepads get "become
        // master" as Bluepad32 asks; unknown devices while syncing may be
        // the console.
        const bool slave = known_console || (!has_key && s_sync_active);
        hci_set_master_slave_policy(slave ? HCI_ROLE_SLAVE : HCI_ROLE_MASTER);
    }
    return 1;
}

// --- HCI

void hci_handler(uint8_t packet_type, uint16_t, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            apply_identity();
            register_sdp();
            s_cmd_scan_setup = true;
        }
        break;
    case HCI_EVENT_COMMAND_STATUS:
        if (hci_event_command_status_get_command_opcode(packet) == HCI_OPCODE_HCI_QOS_SETUP &&
            hci_event_command_status_get_status(packet) != ERROR_CODE_SUCCESS &&
            s_qos_inflight != HCI_CON_HANDLE_INVALID) {
            const hci_con_handle_t h = s_qos_inflight;
            s_qos_inflight = HCI_CON_HANDLE_INVALID;
            qos_failed(h, hci_event_command_status_get_status(packet));
        }
        if (hci_event_command_status_get_command_opcode(packet) == HCI_OPCODE_HCI_SNIFF_MODE &&
            hci_event_command_status_get_status(packet) != ERROR_CODE_SUCCESS) {
            ESP_LOGW(TAG, "Gamepad sniff refused (0x%02x)", unsigned(hci_event_command_status_get_status(packet)));
        }
        break;
    case HCI_EVENT_QOS_SETUP_COMPLETE:
        if (size >= 5) {
            const hci_con_handle_t h = little_endian_read_16(packet, 3) & 0x0fff;
            s_qos_inflight = HCI_CON_HANDLE_INVALID;
            if (packet[2] != ERROR_CODE_SUCCESS) {
                qos_failed(h, packet[2]);
            }
        }
        break;
    case HCI_EVENT_CONNECTION_REQUEST: {
        bd_addr_t addr;
        const uint32_t cod = hci_event_connection_request_get_class_of_device(packet);
        hci_event_connection_request_get_bd_addr(packet, addr);
        if (hci_event_connection_request_get_link_type(packet) != HCI_LINK_TYPE_ACL || !is_console(addr, cod, true)) {
            break;
        }
        if (s_link_claimed && !addr_eq(addr, s_link_addr)) {
            // Accepted by BTstack already; hang up once it is up.
            ESP_LOGW(TAG, "Second console %s refused", bd_addr_to_str(addr));
            std::memcpy(s_reject_addr, addr, BD_ADDR_LEN);
            s_reject_pending = true;
        } else {
            ESP_LOGI(TAG, "Incoming ACL from console %s (cod 0x%06lx)", bd_addr_to_str(addr), (unsigned long)cod);
            claim_link(addr, false);
        }
        // Hide the console from the host stack.
        if (s_fallback != nullptr) {
            packet[0] = HCI_EVENT_NOP;
        }
        break;
    }
    case HCI_EVENT_CONNECTION_COMPLETE: {
        bd_addr_t addr;
        hci_event_connection_complete_get_bd_addr(packet, addr);
        const bool ok = hci_event_connection_complete_get_status(packet) == ERROR_CODE_SUCCESS;
        if (s_reject_pending && addr_eq(addr, s_reject_addr)) {
            s_reject_pending = false;
            if (ok) {
                gap_disconnect(hci_event_connection_complete_get_connection_handle(packet));
            }
            break;
        }
        if (!is_link_addr(addr)) {
            break;
        }
        restore_page_settings();
        if (!ok) {
            release_link();
            break;
        }
        s_link_handle = hci_event_connection_complete_get_connection_handle(packet);
        if (!s_link_outgoing) {
            set_pads_relaxed(true);  // the console is master: give it air time
        }
        if (s_link_dropping) {
            gap_disconnect(s_link_handle);  // disconnect() came while paging
        } else if (s_link_outgoing && s_link_master && !s_profile->console_master) {
            queue_policy(s_link_handle, 0);  // no sniff; stay master
        }
        // A Switch 2 needs sniff: with it refused (v0.1.9) the console hung up
        // (0x13) ~100 ms after every connection, before any subcommand.
        break;
    }
    case HCI_EVENT_MODE_CHANGE:
        if (s_link_handle != HCI_CON_HANDLE_INVALID &&
            hci_event_mode_change_get_handle(packet) == s_link_handle) {
            ESP_LOGI(TAG, "Console link mode %u (status 0x%02x, interval %u slots)",
                     unsigned(hci_event_mode_change_get_mode(packet)),
                     unsigned(hci_event_mode_change_get_status(packet)),
                     unsigned(hci_event_mode_change_get_interval(packet)));
        } else {
            for (auto h : s_pads) {
                if (h != HCI_CON_HANDLE_INVALID && h == hci_event_mode_change_get_handle(packet)) {
                    ESP_LOGI(TAG, "Gamepad link mode %u (status 0x%02x, interval %u slots)",
                             unsigned(hci_event_mode_change_get_mode(packet)),
                             unsigned(hci_event_mode_change_get_status(packet)),
                             unsigned(hci_event_mode_change_get_interval(packet)));
                }
            }
        }
        break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
        if (s_link_handle != HCI_CON_HANDLE_INVALID &&
            hci_event_encryption_change_get_connection_handle(packet) == s_link_handle) {
            ESP_LOGI(TAG, "Console link encryption %u (status 0x%02x)",
                     unsigned(hci_event_encryption_change_get_encryption_enabled(packet)),
                     unsigned(hci_event_encryption_change_get_status(packet)));
        }
        break;
    case HCI_EVENT_ENCRYPTION_KEY_REFRESH_COMPLETE:
        if (s_link_handle != HCI_CON_HANDLE_INVALID &&
            hci_event_encryption_key_refresh_complete_get_handle(packet) == s_link_handle) {
            ESP_LOGI(TAG, "Console link key refresh (status 0x%02x)",
                     unsigned(hci_event_encryption_key_refresh_complete_get_status(packet)));
        }
        break;
    case HCI_EVENT_ROLE_CHANGE: {
        bd_addr_t addr;
        hci_event_role_change_get_bd_addr(packet, addr);
        if (is_link_addr(addr) && hci_event_role_change_get_status(packet) == ERROR_CODE_SUCCESS) {
            s_link_master = hci_event_role_change_get_role(packet) == HCI_ROLE_MASTER;
            set_pads_relaxed(!s_link_master);
        }
        break;
    }
    case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT:
        if (s_link_claimed && hci_event_authentication_complete_get_connection_handle(packet) == s_link_handle &&
            hci_event_authentication_complete_get_status(packet) != ERROR_CODE_SUCCESS) {
            ESP_LOGW(TAG, "Console authentication failed (0x%02x); pair again",
                     hci_event_authentication_complete_get_status(packet));
        }
        break;
    case HCI_EVENT_LINK_KEY_REQUEST: {
        bd_addr_t addr;
        hci_event_link_key_request_get_bd_addr(packet, addr);
        if (is_link_addr(addr)) {
            link_key_t key;
            link_key_type_t type;
            ESP_LOGI(TAG, "Console asks for the link key (%s)",
                     gap_get_link_key_for_bd_addr(addr, key, &type) ? "have it" : "none stored");
        }
        break;
    }
    case HCI_EVENT_LINK_KEY_NOTIFICATION: {
        // BTstack skips storing a key when neither side asked for bonding,
        // and a Switch 2 refused our reconnect with 0x05 (authentication
        // failure) after a good pairing. The console keeps the key, so we do.
        bd_addr_t addr;
        hci_event_link_key_request_get_bd_addr(packet, addr);  // same layout
        if (size >= 25 && is_link_addr(addr) && packet[24] != CHANGED_COMBINATION_KEY &&
            !btstack_is_null(&packet[8], 16)) {
            std::memcpy(s_key_addr, addr, BD_ADDR_LEN);
            std::memcpy(s_key, &packet[8], sizeof(s_key));
            s_key_type = link_key_type_t(packet[24]);
            s_key_pending = true;
        }
        break;
    }
    case GAP_EVENT_SECURITY_LEVEL:
        release_held(gap_event_security_level_get_handle(packet), gap_event_security_level_get_security_level(packet));
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
        release_held(handle, -1);
        if (handle == s_qos_inflight) {
            s_qos_inflight = HCI_CON_HANDLE_INVALID;
        }
        for (auto &h : s_pads) {
            if (h == handle) {
                h = HCI_CON_HANDLE_INVALID;
            }
        }
        if (s_link_claimed && handle == s_link_handle) {
            s_last_reason = hci_event_disconnection_complete_get_reason(packet);
            ESP_LOGI(TAG, "Console disconnected (reason 0x%02x)", s_last_reason);
            release_link();
        }
        break;
    }
    default:
        break;
    }
    pump();
}

}  // namespace

// --- Public

void set_fallback_handler(btstack_packet_handler_t handler) { s_fallback = handler; }

void begin(const Profile &profile, Listener &listener)
{
    s_profile = &profile;
    s_listener = &listener;
    if (s_setup_done) {
        return;
    }
    s_setup_done = true;
    load_host();
    // Take the HID PSMs over from the host stack; its traffic is forwarded.
    l2cap_unregister_service(kPsmControl);
    l2cap_unregister_service(kPsmInterrupt);
    l2cap_register_service(l2cap_handler, kPsmControl, kMtu, LEVEL_0);
    l2cap_register_service(l2cap_handler, kPsmInterrupt, kMtu, LEVEL_0);
    s_hci_registration.callback = hci_handler;
    hci_add_event_handler_for_security_manager(&s_hci_registration);  // head of the list
    gap_register_classic_connection_filter(connection_filter);
    // SSP "Just Works": no display, no keyboard.
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(true);
    if (hci_get_state() == HCI_STATE_WORKING) {
        apply_identity();
        register_sdp();
    }
}

void sync()
{
    if (!s_sync_active) {
        s_sync_active = true;
        s_listener->on_sync(true);
    }
    gap_connectable_control(1);
    gap_discoverable_control(1);
    // About 11% duty so the console's inquiry and page land fast.
    gap_set_page_scan_activity(0x0200, 0x0024);
    gap_inquiry_set_scan_activity(0x0200, 0x0024);
    btstack_run_loop_remove_timer(&s_sync_timer);
    btstack_run_loop_set_timer_handler(&s_sync_timer, sync_timer_cb);
    btstack_run_loop_set_timer(&s_sync_timer, kSyncWindowMs);
    btstack_run_loop_add_timer(&s_sync_timer);
    ESP_LOGI(TAG, "Sync window open: discoverable as \"%s\"", s_profile->name);
}

bool syncing() { return s_sync_active; }

void connect()
{
    if (!s_have_host || s_link_claimed) {
        return;
    }
    claim_link(s_host_addr, true);
    if (s_fallback != nullptr) {
        if (!s_profile->console_master) {
            // Page with role switch off so the ESP32 stays master of both links.
            gap_set_allow_role_switch(false);
            s_restore_role_switch = true;
        }
        gap_set_page_timeout(kPageTimeoutConsole);
        s_restore_page_timeout = true;
    }
    if (create_channel(kPsmControl, &s_cid_control) != ERROR_CODE_SUCCESS) {
        s_cid_control = 0;
        release_link();
        return;
    }
    ESP_LOGI(TAG, "Paging console %s", bd_addr_to_str(s_host_addr));
}

bool connecting() { return s_link_claimed && !s_link_up; }
bool connected() { return s_link_up; }
void disconnect() { drop_link(); }
bool has_host() { return s_have_host; }

void forget() { forget_host(); }

bool link_is_master() { return s_link_up && s_link_master; }
uint8_t last_disconnect_reason() { return s_last_reason; }
uint32_t longest_block_ms() { return s_longest_block_ms; }
uint32_t last_block_rx() { return s_last_block_rx; }
void flush() { send_reports(false); }

void tune_link(hci_con_handle_t handle, const bd_addr_t addr)
{
    if (handle == HCI_CON_HANDLE_INVALID || gap_get_connection_type(handle) != GAP_CONNECTION_ACL ||
        (s_link_claimed && handle == s_link_handle)) {
        return;
    }
    if (gap_get_role(handle) == HCI_ROLE_SLAVE) {
        gap_request_role(addr, HCI_ROLE_MASTER);
    }
    queue_policy(handle, LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                             (s_profile->console_master ? LM_LINK_POLICY_ENABLE_SNIFF_MODE : 0));
    if (s_pads[0] != handle && s_pads[1] != handle) {
        (s_pads[0] == HCI_CON_HANDLE_INVALID ? s_pads[0] : s_pads[1]) = handle;
    }
    tune_pad_qos(handle);
    if (s_pads_relaxed) {
        pad_sniff(handle, true);
    }
}

void local_address(bd_addr_t out) { gap_local_bd_addr(out); }

}  // namespace yucca::bthid
