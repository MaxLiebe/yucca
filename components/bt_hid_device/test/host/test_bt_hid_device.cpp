#include <cstdarg>
// Host test for the BT HID device transport. BTstack is replaced by fakes
// that record what the transport does; the test feeds it the events a
// console pairing, a console reconnect and a reconnecting gamepad produce.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/bt_hid_device.cpp"
#include "check.h"

using namespace yucca::bthid;

int g_verbose;

// --- Fake BTstack ---------------------------------------------------------

extern "C" {
static HCI_STATE g_hci_state = HCI_STATE_OFF;
HCI_STATE hci_get_state(void) { return g_hci_state; }
hci_connection_t *hci_connection_for_handle(hci_con_handle_t) { return nullptr; }
int hci_number_free_acl_slots_for_handle(hci_con_handle_t) { return 0; }
bool hci_can_send_command_packet_now(void) { return true; }
static std::vector<const hci_cmd_t *> g_cmds;
const hci_cmd_t hci_write_link_policy_settings = {};
static int g_policy_handle = -1, g_policy_bits = -1;  // last link policy written
const hci_cmd_t hci_change_connection_packet_type = {};
static int g_ptype_bits = -1;  // last packet types asked for
const hci_cmd_t hci_sniff_mode = {};
const hci_cmd_t hci_exit_sniff_mode = {};
static int g_sniff_handle = -1;  // last sniff enter/exit target
uint8_t hci_send_cmd(const hci_cmd_t *cmd, ...)
{
    g_cmds.push_back(cmd);
    if (cmd == &hci_sniff_mode || cmd == &hci_exit_sniff_mode) {
        va_list ap;
        va_start(ap, cmd);
        g_sniff_handle = va_arg(ap, int);
        va_end(ap);
    }
    if (cmd == &hci_write_link_policy_settings) {
        va_list ap;
        va_start(ap, cmd);
        g_policy_handle = va_arg(ap, int);
        g_policy_bits = va_arg(ap, int);
        va_end(ap);
    }
    if (cmd == &hci_change_connection_packet_type) {
        va_list ap;
        va_start(ap, cmd);
        va_arg(ap, int);
        g_ptype_bits = va_arg(ap, int);
        va_end(ap);
    }
    return 0;
}
const hci_cmd_t hci_disconnect = {};
const hci_cmd_t hci_qos_setup = {};
const hci_cmd_t hci_create_connection_cancel = {};
const hci_cmd_t hci_write_inquiry_scan_type = {};

static btstack_packet_callback_registration_t *g_hci;
void hci_add_event_handler_for_security_manager(btstack_packet_callback_registration_t *r) { g_hci = r; }
static int (*g_filter)(bd_addr_t, hci_link_type_t);
void gap_register_classic_connection_filter(int (*cb)(bd_addr_t, hci_link_type_t)) { g_filter = cb; }
static uint8_t g_policy = 0xff;
void hci_set_master_slave_policy(uint8_t p) { g_policy = p; }

static const char *g_name;
static uint32_t g_cod;
static int g_io_cap = -1;
static bool g_auto_accept;
void gap_set_local_name(const char *n) { g_name = n; }
void gap_set_class_of_device(uint32_t c) { g_cod = c; }
void gap_ssp_set_io_capability(int c) { g_io_cap = c; }
void gap_ssp_set_auto_accept(int a) { g_auto_accept = a; }
static bool g_role_switch = true;
void gap_set_allow_role_switch(bool a) { g_role_switch = a; }
void gap_set_default_link_policy_settings(uint16_t) {}
static int g_discoverable = -1, g_connectable = -1;
void gap_connectable_control(uint8_t e) { g_connectable = e; }
void gap_discoverable_control(uint8_t e) { g_discoverable = e; }
void gap_set_page_scan_activity(uint16_t, uint16_t) {}
void gap_inquiry_set_scan_activity(uint16_t, uint16_t) {}
static uint16_t g_page_timeout;
void gap_set_page_timeout(uint16_t t) { g_page_timeout = t; }
static bool g_have_key;
bool gap_get_link_key_for_bd_addr(bd_addr_t, link_key_t, link_key_type_t *) { return g_have_key; }
static int g_keys_stored;
static uint8_t g_stored_type;
void gap_store_link_key_for_bd_addr(bd_addr_t, link_key_t, link_key_type_t t) { g_keys_stored++; g_stored_type = t; }
static int g_keys_dropped;
void gap_drop_link_key_for_bd_addr(bd_addr_t) { g_keys_dropped++; }
static gap_security_level_t g_gap_level = LEVEL_2, g_level_at_create;
gap_security_level_t gap_get_security_level(void) { return g_gap_level; }
void gap_set_security_level(gap_security_level_t l) { g_gap_level = l; }
static gap_security_level_t g_link_level = LEVEL_2;
gap_security_level_t gap_security_level(hci_con_handle_t) { return g_link_level; }
void gap_request_security_level(hci_con_handle_t, gap_security_level_t) {}
static int g_disconnects;
uint8_t gap_disconnect(hci_con_handle_t) { g_disconnects++; return 0; }
gap_connection_type_t gap_get_connection_type(hci_con_handle_t) { return GAP_CONNECTION_ACL; }
hci_role_t gap_get_role(hci_con_handle_t) { return HCI_ROLE_SLAVE; }
static int g_role_requests;
static hci_role_t g_role_wanted;
uint8_t gap_request_role(const bd_addr_t, hci_role_t r) { g_role_requests++; g_role_wanted = r; return 0; }
void gap_local_bd_addr(bd_addr_t a) { std::memset(a, 0x11, 6); }

static btstack_packet_handler_t g_services[2];
uint8_t l2cap_register_service(btstack_packet_handler_t h, uint16_t psm, uint16_t, gap_security_level_t)
{
    g_services[psm == 0x11 ? 0 : 1] = h;
    return 0;
}
uint8_t l2cap_unregister_service(uint16_t) { return 0; }
static int g_accepts, g_declines;
void l2cap_accept_connection(uint16_t) { g_accepts++; }
void l2cap_decline_connection(uint16_t) { g_declines++; }
static uint16_t g_next_cid = 0x60;
static std::vector<uint16_t> g_created_psm;
uint8_t l2cap_create_channel(btstack_packet_handler_t, bd_addr_t, uint16_t psm, uint16_t, uint16_t *cid)
{
    g_level_at_create = g_gap_level;
    *cid = g_next_cid++;
    g_created_psm.push_back(psm);
    return 0;
}
uint8_t l2cap_disconnect(uint16_t) { return 0; }
static bool g_can_send = true;
bool l2cap_can_send_packet_now(uint16_t) { return g_can_send; }
static int g_can_send_requests;
uint8_t l2cap_request_can_send_now_event(uint16_t) { g_can_send_requests++; return 0; }
static std::vector<std::vector<uint8_t>> g_sent;
uint8_t l2cap_send(uint16_t, const uint8_t *data, uint16_t len)
{
    g_sent.emplace_back(data, data + len);
    return 0;
}

static uint8_t *g_records[4];
static int g_record_count;
static uint32_t g_next_handle = 0x10001;
uint32_t sdp_create_service_record_handle(void) { return g_next_handle++; }
uint8_t sdp_register_service(const uint8_t *r) { g_records[g_record_count++ & 3] = (uint8_t *)r; return 0; }
void sdp_unregister_service(uint32_t) {}
uint8_t *sdp_get_record_for_handle(uint32_t) { return nullptr; }
static uint16_t g_pnp_vid, g_pnp_pid;
void device_id_create_sdp_record(uint8_t *service, uint32_t, uint16_t, uint16_t vid, uint16_t pid, uint16_t)
{
    g_pnp_vid = vid;
    g_pnp_pid = pid;
    de_create_sequence(service);
}

static bool g_report_armed, g_sync_armed, g_watchdog_armed;
static void armed(btstack_timer_source_t *ts, bool on)
{
    if (ts == &s_report_timer) g_report_armed = on;
    if (ts == &s_sync_timer) g_sync_armed = on;
    if (ts == &s_connect_watchdog) g_watchdog_armed = on;
}
void btstack_run_loop_set_timer_handler(btstack_timer_source_t *ts, void (*fn)(btstack_timer_source_t *)) { ts->process = fn; }
void btstack_run_loop_set_timer(btstack_timer_source_t *, uint32_t) {}
void btstack_run_loop_add_timer(btstack_timer_source_t *ts) { armed(ts, true); }
int btstack_run_loop_remove_timer(btstack_timer_source_t *ts) { armed(ts, false); return 1; }

static uint8_t g_tlv[6];
static bool g_tlv_have;
static uint32_t g_tlv_tag;
static int tlv_get(void *, uint32_t tag, uint8_t *buf, uint32_t len)
{
    if (!g_tlv_have || tag != g_tlv_tag || len < 6) return 0;
    std::memcpy(buf, g_tlv, 6);
    return 6;
}
static int tlv_store(void *, uint32_t tag, const uint8_t *d, uint32_t)
{
    g_tlv_tag = tag;
    std::memcpy(g_tlv, d, 6);
    g_tlv_have = true;
    return 0;
}
static void tlv_delete(void *, uint32_t) { g_tlv_have = false; }
static const btstack_tlv_t g_tlv_impl = {tlv_get, tlv_store, tlv_delete};
void btstack_tlv_get_instance(const btstack_tlv_t **t, void **c) { *t = &g_tlv_impl; *c = nullptr; }
}

// --- Fake Bluepad32 and listener --------------------------------------------

static int g_forwarded;
static void fake_bp32(uint8_t, uint16_t, uint8_t *, uint16_t) { g_forwarded++; }

struct FakeListener : Listener {
    std::vector<std::vector<uint8_t>> outputs;
    int links_up = 0, links_down = 0, sync_on = 0, sync_off = 0;
    int pending_replies = 0, periodic_calls = 0;
    void on_output(const uint8_t *r, size_t n) override
    {
        outputs.emplace_back(r, r + n);
        pending_replies++;
    }
    size_t next_input(uint8_t *out, size_t, bool periodic) override
    {
        if (pending_replies > 0) {
            pending_replies--;
            out[0] = 0x21;
            return 49;
        }
        if (periodic) {
            periodic_calls++;
            out[0] = 0x30;
            return 49;
        }
        return 0;
    }
    void on_link(bool up) override { (up ? links_up : links_down)++; }
    void on_sync(bool a) override { (a ? sync_on : sync_off)++; }
} g_listener;

static const uint8_t kDescriptor[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0};
static const Profile kProfile = {"Pro Controller", 0x002508, kDescriptor, sizeof(kDescriptor), "Wireless Gamepad",
                                 "Gamepad", "Nintendo", 0x08, 0x21, 0x057e, 0x2009, 0x0001, 0x53574348, 15};

// --- Event builders -----------------------------------------------------------

static const bd_addr_t SWITCH = {0x98, 0xb6, 0xe9, 0x01, 0x02, 0x03};
static const bd_addr_t PAD = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};

static void put_addr(uint8_t *p, const bd_addr_t a) { reverse_bytes(a, p, 6); }
static void emit(uint8_t *ev, uint16_t len) { g_hci->callback(HCI_EVENT_PACKET, 0, ev, len); }

static void ev_state(HCI_STATE st)
{
    uint8_t ev[3] = {BTSTACK_EVENT_STATE, 1, uint8_t(st)};
    g_hci_state = st;
    emit(ev, sizeof(ev));
}

static uint8_t ev_conn_request(const bd_addr_t a, uint32_t cod)
{
    uint8_t ev[12] = {HCI_EVENT_CONNECTION_REQUEST, 10};
    put_addr(ev + 2, a);
    little_endian_store_24(ev, 8, cod);
    ev[11] = HCI_LINK_TYPE_ACL;
    g_filter((uint8_t *)a, HCI_LINK_TYPE_ACL);
    emit(ev, sizeof(ev));
    return ev[0];
}

static void ev_conn_complete(const bd_addr_t a, uint16_t handle, uint8_t status)
{
    uint8_t ev[13] = {HCI_EVENT_CONNECTION_COMPLETE, 11, status};
    little_endian_store_16(ev, 3, handle);
    put_addr(ev + 5, a);
    ev[11] = HCI_LINK_TYPE_ACL;
    emit(ev, sizeof(ev));
}

static void ev_disconnect(uint16_t handle, uint8_t reason)
{
    uint8_t ev[6] = {HCI_EVENT_DISCONNECTION_COMPLETE, 4, 0};
    little_endian_store_16(ev, 3, handle);
    ev[5] = reason;
    emit(ev, sizeof(ev));
}

static void l2cap_incoming(const bd_addr_t a, uint16_t handle, uint16_t psm, uint16_t cid)
{
    uint8_t ev[16] = {L2CAP_EVENT_INCOMING_CONNECTION, 14};
    put_addr(ev + 2, a);
    little_endian_store_16(ev, 8, handle);
    little_endian_store_16(ev, 10, psm);
    little_endian_store_16(ev, 12, cid);
    little_endian_store_16(ev, 14, 0x40);
    g_services[psm == 0x11 ? 0 : 1](HCI_EVENT_PACKET, cid, ev, sizeof(ev));
}

static void l2cap_opened(const bd_addr_t a, uint16_t handle, uint16_t psm, uint16_t cid, uint8_t status = 0)
{
    uint8_t ev[26] = {L2CAP_EVENT_CHANNEL_OPENED, 24, status};
    put_addr(ev + 3, a);
    little_endian_store_16(ev, 9, handle);
    little_endian_store_16(ev, 11, psm);
    little_endian_store_16(ev, 13, cid);
    g_services[0](HCI_EVENT_PACKET, cid, ev, sizeof(ev));
}

static void l2cap_data(uint16_t cid, std::vector<uint8_t> d) { g_services[1](L2CAP_DATA_PACKET, cid, d.data(), uint16_t(d.size())); }

static void tick_report() { s_report_timer.process(&s_report_timer); }

// --- Tests ---------------------------------------------------------------------

static void test_begin()
{
    set_fallback_handler(fake_bp32);
    begin(kProfile, g_listener);
    CHECK(g_hci != nullptr && g_filter != nullptr && g_services[0] != nullptr && g_services[1] != nullptr);
    CHECK(g_io_cap == SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT && g_auto_accept);
    CHECK(!has_host());
    ev_state(HCI_STATE_WORKING);
    CHECK(std::strcmp(g_name, "Pro Controller") == 0 && g_cod == 0x002508);
    CHECK(g_record_count == 2 && g_pnp_vid == 0x057e && g_pnp_pid == 0x2009);
    CHECK(de_get_len(s_hid_record) <= kSdpFixedBytes + sizeof(kDescriptor) + std::strlen(kProfile.service_name) +
                                          std::strlen(kProfile.service_description) + std::strlen(kProfile.provider));
    uint8_t *desc = sdp_get_attribute_value_for_attribute_id(s_hid_record, 0x0206);
    CHECK(desc != nullptr && de_get_len(desc) > sizeof(kDescriptor));
    uint8_t *sub = sdp_get_attribute_value_for_attribute_id(s_hid_record, 0x0202);
    CHECK(sub != nullptr && de_get_header_size(sub) == 1 && sub[1] == 0x08);
    CHECK(sdp_get_attribute_value_for_attribute_id(s_hid_record, 0x000d) != nullptr);
}

static void test_gamepad_passes_through()
{
    g_forwarded = 0;
    CHECK(ev_conn_request(PAD, 0x002508) == HCI_EVENT_CONNECTION_REQUEST);  // not hidden
    CHECK(g_policy == HCI_ROLE_MASTER);
    l2cap_incoming(PAD, 0x0050, 0x13, 0x40);
    CHECK(g_forwarded == 1 && g_accepts == 0);
    // Sync window open: a peripheral is still a gamepad.
    sync();
    CHECK(ev_conn_request(PAD, 0x002508) == HCI_EVENT_CONNECTION_REQUEST);
    CHECK(!s_link_claimed);
}

static void test_console_pairs()
{
    CHECK(syncing() && g_discoverable == 1 && g_sync_armed);
    CHECK(g_listener.sync_on == 1);
    g_have_key = false;
    CHECK(ev_conn_request(SWITCH, 0x000000) == HCI_EVENT_NOP);  // hidden from Bluepad32
    CHECK(g_policy == HCI_ROLE_SLAVE);                          // the console stays master
    ev_conn_complete(SWITCH, 0x0080, 0);
    l2cap_incoming(SWITCH, 0x0080, 0x11, 0x41);
    l2cap_incoming(SWITCH, 0x0080, 0x13, 0x42);
    CHECK(g_accepts == 2);
    l2cap_opened(SWITCH, 0x0080, 0x11, 0x41);
    CHECK(!connected());
    l2cap_opened(SWITCH, 0x0080, 0x13, 0x42);
    CHECK(connected() && g_listener.links_up == 1);
    CHECK(!syncing() && g_discoverable == 0 && g_listener.sync_off == 1);
    CHECK(has_host() && g_tlv_have && g_tlv_tag == kProfile.host_tag);
    CHECK(g_keys_dropped == 0);
    CHECK(g_report_armed);
    CHECK(!link_is_master());

    // A subcommand: the reply goes out at once, with the 0xA1 header.
    g_sent.clear();
    l2cap_data(0x42, {0xa2, 0x01, 0x00, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x02});
    CHECK(g_listener.outputs.size() == 1 && g_listener.outputs[0][0] == 0x01);
    CHECK(g_sent.size() == 1 && g_sent[0].size() == 50 && g_sent[0][0] == 0xa1 && g_sent[0][1] == 0x21);

    // The periodic report.
    tick_report();
    CHECK(g_sent.size() == 2 && g_sent[1][1] == 0x30);
    // Radio busy: the periodic report is dropped, not queued.
    g_can_send = false;
    const int calls = g_listener.periodic_calls;
    tick_report();
    CHECK(g_listener.periodic_calls == calls && g_can_send_requests > 0);
    for (int i = 0; i < 40; i++) {
        tick_report();
    }
    l2cap_data(0x42, {0xa2, 0x10, 0x00, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40});  // rumble only
    g_can_send = true;
    CHECK(longest_block_ms() == 0);  // counted once a report gets through
    tick_report();
    CHECK(longest_block_ms() >= 300 && last_block_rx() == 1);

    // Control channel: SET_PROTOCOL is acknowledged, GET_REPORT refused.
    g_sent.clear();
    g_services[0](L2CAP_DATA_PACKET, 0x41, (uint8_t *)"\x70", 1);
    CHECK(!g_sent.empty() && g_sent[0].size() == 1 && g_sent[0][0] == 0x00);

    ev_disconnect(0x0080, 0x13);
    CHECK(!connected() && g_listener.links_down == 1 && !g_report_armed);
    CHECK(last_disconnect_reason() == 0x13);
}

static void test_reconnect_as_master()
{
    g_created_psm.clear();
    g_gap_level = LEVEL_2;
    connect();
    CHECK(connecting() && g_watchdog_armed);
    CHECK(!g_role_switch && g_page_timeout == 0x2000);
    CHECK(g_created_psm.size() == 1 && g_created_psm[0] == 0x11);
    CHECK(g_level_at_create == LEVEL_0 && g_gap_level == LEVEL_2);
    g_cmds.clear();
    ev_conn_complete(SWITCH, 0x0081, 0);
    CHECK(g_role_switch && g_page_timeout == 0x6000);  // restored for gamepads
    CHECK(!g_cmds.empty() && g_cmds.back() == &hci_write_link_policy_settings);
    const uint16_t ctrl = s_cid_control;
    l2cap_opened(SWITCH, 0x0081, 0x11, ctrl);
    CHECK(g_created_psm.size() == 2 && g_created_psm[1] == 0x13);
    l2cap_opened(SWITCH, 0x0081, 0x13, s_cid_interrupt);
    CHECK(connected() && link_is_master() && !g_watchdog_armed);

    // While connected, the paired console's own ACL is not a gamepad.
    ev_disconnect(0x0081, 0x08);
    CHECK(!connected());
}

static int count_cmd(const hci_cmd_t *cmd)
{
    int n = 0;
    for (auto *c : g_cmds) {
        n += c == cmd;
    }
    return n;
}

static void ev_qos_complete(uint16_t handle, uint8_t status)
{
    uint8_t ev[21] = {HCI_EVENT_QOS_SETUP_COMPLETE, 19, status};
    little_endian_store_16(ev, 3, handle);
    emit(ev, sizeof(ev));
}

static void test_watchdog_while_paging()
{
    connect();
    const uint16_t ctrl = s_cid_control;
    CHECK(connecting() && g_watchdog_armed);
    g_cmds.clear();
    s_connect_watchdog.process(&s_connect_watchdog);
    CHECK(count_cmd(&hci_create_connection_cancel) == 1);
    CHECK(connecting());  // claimed until the page ends
    ev_conn_complete(SWITCH, 0x0000, 0x02);  // cancelled
    CHECK(!connecting() && !s_link_claimed);
    // The abandoned channel's failure does not reach Bluepad32.
    const int fwd = g_forwarded;
    l2cap_opened(SWITCH, 0x0000, 0x11, ctrl, 0x04);
    CHECK(g_forwarded == fwd);
}

static void test_late_page_success()
{
    // The drop watchdog lets go, then the page succeeds anyway: hung up.
    connect();
    const uint16_t ctrl = s_cid_control;
    disconnect();
    s_connect_watchdog.process(&s_connect_watchdog);
    CHECK(!s_link_claimed);
    const int fwd = g_forwarded, disc = g_disconnects;
    l2cap_opened(SWITCH, 0x0084, 0x11, ctrl);
    CHECK(g_forwarded == fwd && g_disconnects == disc + 1);
    ev_disconnect(0x0084, 0x16);

    // Same again, but a new attempt to the console shares the link: only the
    // stale channel is closed.
    connect();
    const uint16_t old = s_cid_control;
    disconnect();
    s_connect_watchdog.process(&s_connect_watchdog);
    connect();
    CHECK(connecting());
    l2cap_opened(SWITCH, 0x0085, 0x11, old);
    CHECK(g_disconnects == disc + 1 && connecting());
    disconnect();
    ev_conn_complete(SWITCH, 0x0000, 0x02);
}

static void test_disconnect_while_paging()
{
    // The page completes despite the cancel: the link is hung up.
    connect();
    disconnect();
    CHECK(connecting());
    const int disc = g_disconnects;
    ev_conn_complete(SWITCH, 0x0082, 0);
    CHECK(g_disconnects == disc + 1 && !connected());
    ev_disconnect(0x0082, 0x16);
    CHECK(!connecting());

    // No page result at all: the watchdog lets go.
    connect();
    disconnect();
    s_connect_watchdog.process(&s_connect_watchdog);
    CHECK(!connecting() && !g_watchdog_armed);
}

static void test_qos_one_at_a_time()
{
    static const bd_addr_t PAD2 = {0x10, 0x20, 0x30, 0x40, 0x50, 0x61};
    g_cmds.clear();
    tune_link(0x0050, PAD);
    tune_link(0x0051, PAD2);
    CHECK(g_role_requests == 2 && g_role_wanted == HCI_ROLE_MASTER);
    CHECK(count_cmd(&hci_change_connection_packet_type) == 0);  // not for a Switch 1 or Wii U
    CHECK(count_cmd(&hci_qos_setup) == 1);
    ev_qos_complete(0x0051, 0);  // queue is LIFO: 0x0051 went first
    CHECK(count_cmd(&hci_qos_setup) == 2);
    // Guaranteed refused: retried as best effort, after the first completes.
    ev_qos_complete(0x0050, 0x1e);
    CHECK(count_cmd(&hci_qos_setup) == 3 && s_qos_inflight == 0x0050 && s_qos_inflight_type == kQosBestEffort);
    ev_disconnect(0x0050, 0x13);  // a pad that leaves clears the in-flight request
    CHECK(s_qos_inflight == HCI_CON_HANDLE_INVALID);
}

// While the console is master of its link, gamepads are polled less often.
static void test_pads_relaxed_while_slave()
{
    static const bd_addr_t CONSOLE2 = {0x3c, 0xa9, 0xab, 0x21, 0x4a, 0x3b};
    ev_disconnect(0x0051, 0x13);  // left over from test_qos_one_at_a_time
    while (s_qos_inflight != HCI_CON_HANDLE_INVALID) {
        ev_qos_complete(s_qos_inflight, 0);
    }
    tune_link(0x0052, PAD);
    CHECK(s_qos_inflight == 0x0052 && s_qos_inflight_type == kQosGuaranteed && s_qos_inflight_latency == kQosLatencyUs);
    ev_qos_complete(0x0052, 0);

    sync();
    g_have_key = false;
    ev_conn_request(CONSOLE2, 0x000000);
    ev_conn_complete(CONSOLE2, 0x0090, 0);
    CHECK(s_qos_inflight == 0x0052 && s_qos_inflight_type == kQosBestEffort && s_qos_inflight_latency == kQosRelaxedUs);
    ev_qos_complete(0x0052, 0);

    ev_disconnect(0x0090, 0x13);
    CHECK(s_qos_inflight == 0x0052 && s_qos_inflight_type == kQosGuaranteed && s_qos_inflight_latency == kQosLatencyUs);
    ev_qos_complete(0x0052, 0);
    ev_disconnect(0x0052, 0x13);
    CHECK(s_pads[0] == HCI_CON_HANDLE_INVALID && s_pads[1] == HCI_CON_HANDLE_INVALID);
    end_sync(true);
}

// Switch 2: pages let the console take the master role.
static void test_console_master_page()
{
    Profile p2 = kProfile;
    p2.console_master = true;
    begin(p2, g_listener);
    g_gap_level = LEVEL_2;
    connect();
    CHECK(connecting() && g_role_switch && g_page_timeout == 0x2000);
    CHECK(g_level_at_create == LEVEL_2);  // authenticates with the stored key
    g_policy_handle = -1;
    ev_conn_complete(SWITCH, 0x0082, 0);
    CHECK(!s_pads_relaxed);  // we paged: master until the console switches roles
    CHECK(g_policy_handle == -1);  // default policy: the Switch 2 needs sniff
    uint8_t ev[10] = {HCI_EVENT_ROLE_CHANGE, 8, 0};
    put_addr(ev + 3, SWITCH);
    ev[9] = HCI_ROLE_SLAVE;
    emit(ev, sizeof(ev));
    CHECK(!s_link_master && s_pads_relaxed);
    ev_disconnect(0x0082, 0x13);
    CHECK(!connecting() && !s_pads_relaxed);
    begin(kProfile, g_listener);
}

// Switch 2: gamepad links never sniff, also while it is master.
static void test_pads_never_sniff()
{
    Profile p2 = kProfile;
    p2.console_master = true;
    begin(p2, g_listener);
    while (s_qos_inflight != HCI_CON_HANDLE_INVALID) {
        ev_qos_complete(s_qos_inflight, 0);
    }
    // The gamepad paged us and leads; we stay its slave (no own piconet).
    ev_conn_request(PAD, 0x002508);
    CHECK(g_policy == HCI_ROLE_SLAVE);
    g_cmds.clear();
    const int roles = g_role_requests;
    tune_link(0x0053, PAD);
    CHECK(g_role_requests == roles);
    CHECK(g_policy_handle == 0x0053 && g_policy_bits == LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    // One-slot packets only: no 3- or 5-slot types, EDR 1-slot types allowed.
    CHECK(count_cmd(&hci_change_connection_packet_type) == 1 && g_ptype_bits == 0x3318);

    sync();
    g_have_key = false;
    ev_conn_request(SWITCH, 0x000000);
    ev_conn_complete(SWITCH, 0x0091, 0);  // it paged us: it is master
    CHECK(s_pads_relaxed && count_cmd(&hci_sniff_mode) == 0);

    ev_disconnect(0x0091, 0x13);
    ev_disconnect(0x0053, 0x13);
    end_sync(true);
    while (s_qos_inflight != HCI_CON_HANDLE_INVALID) {
        ev_qos_complete(s_qos_inflight, 0);
    }
    CHECK(count_cmd(&hci_sniff_mode) == 0 && count_cmd(&hci_exit_sniff_mode) == 0);
    begin(kProfile, g_listener);
}

// Page scan off on request, but always on while syncing.
static void test_pad_sniff_and_drop()
{
    begin(kProfile, g_listener);
    while (s_qos_inflight != HCI_CON_HANDLE_INVALID) {
        ev_qos_complete(s_qos_inflight, 0);
    }
    set_pad_sniff(17);  // rounded down to even
    g_cmds.clear();
    tune_link(0x0054, PAD);
    CHECK(g_policy_bits == (LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE));
    CHECK(count_cmd(&hci_sniff_mode) == 1 && g_sniff_handle == 0x0054 && s_pad_sniff_slots == 16);
    const int disc = g_disconnects;
    drop_pads();
    CHECK(g_disconnects == disc + 1);
    ev_disconnect(0x0054, 0x16);
    drop_pads();
    CHECK(g_disconnects == disc + 1);  // gone: nothing left to drop
    set_pad_sniff(0);
}

static void test_connectable()
{
    set_connectable(false);
    CHECK(g_connectable == 0);
    sync();
    CHECK(g_connectable == 1);
    end_sync(true);
    CHECK(g_connectable == 0);
    set_connectable(true);
    CHECK(g_connectable == 1);
}

// The console's link key is kept even when BTstack would not store it.
static void test_console_link_key_stored()
{
    connect();
    ev_conn_complete(SWITCH, 0x0083, 0);
    uint8_t ev[25] = {HCI_EVENT_LINK_KEY_NOTIFICATION, 23};
    put_addr(ev + 2, SWITCH);
    ev[24] = UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192;
    emit(ev, sizeof(ev));
    CHECK(g_keys_stored == 0);  // all-zero key: ignored
    ev[8] = 0x5a;
    emit(ev, sizeof(ev));
    CHECK(g_keys_stored == 0);  // never while the link is up
    ev_disconnect(0x0083, 0x13);
    CHECK(g_keys_stored == 1 && g_stored_type == UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192);

    connect();
    ev_conn_complete(SWITCH, 0x0084, 0);
    ev[24] = CHANGED_COMBINATION_KEY;
    emit(ev, sizeof(ev));
    put_addr(ev + 2, PAD);  // a gamepad's key is BTstack's business
    ev[24] = UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192;
    emit(ev, sizeof(ev));
    ev_disconnect(0x0084, 0x13);
    CHECK(g_keys_stored == 1);
}

static void test_held_channels()
{
    g_link_level = LEVEL_0;
    const int fwd = g_forwarded, declines = g_declines;
    for (uint16_t i = 0; i < kHeldMax; i++) {
        l2cap_incoming(PAD, 0x0050, 0x13, uint16_t(0x70 + i));
    }
    CHECK(g_forwarded == fwd && g_declines == declines);  // held
    l2cap_incoming(PAD, 0x0050, 0x13, 0x7f);
    CHECK(g_forwarded == fwd && g_declines == declines + 1);  // full: declined, never let through
    // Security reached: the held channels go to Bluepad32.
    uint8_t ev[5] = {GAP_EVENT_SECURITY_LEVEL, 3};
    little_endian_store_16(ev, 2, 0x0050);
    ev[4] = LEVEL_2;
    emit(ev, sizeof(ev));
    CHECK(g_forwarded == fwd + kHeldMax);
    g_link_level = LEVEL_2;
}

static void test_unplug_forgets()
{
    connect();
    ev_conn_complete(SWITCH, 0x0083, 0);
    l2cap_opened(SWITCH, 0x0083, 0x11, s_cid_control);
    l2cap_opened(SWITCH, 0x0083, 0x13, s_cid_interrupt);
    CHECK(connected());
    const int keys = g_keys_dropped;
    g_cmds.clear();
    g_services[0](L2CAP_DATA_PACKET, s_cid_control, (uint8_t *)"\x15", 1);  // VIRTUAL_CABLE_UNPLUG
    CHECK(!has_host() && !g_tlv_have && g_keys_dropped == keys + 1);
    CHECK(count_cmd(&hci_disconnect) == 1);
    ev_disconnect(0x0083, 0x16);
    CHECK(!connected() && !s_link_claimed);
    connect();
    CHECK(!connecting());  // nothing to page
}

static void test_unknown_hosts()
{
    static const bd_addr_t PC = {0x20, 0x20, 0x30, 0x40, 0x50, 0x60};
    static const bd_addr_t OTHER = {0x30, 0x20, 0x30, 0x40, 0x50, 0x60};
    sync();
    // Bonded (a phone or a pad with an odd class): never taken for the console.
    g_have_key = true;
    CHECK(ev_conn_request(PC, 0x5a020c) == HCI_EVENT_CONNECTION_REQUEST);
    CHECK(!s_link_claimed);
    // Unknown and not a peripheral: claimed, but let go if no HID channels come.
    g_have_key = false;
    CHECK(ev_conn_request(PC, 0x000100) == HCI_EVENT_NOP);
    CHECK(s_link_claimed && g_watchdog_armed);
    // A second unknown host meanwhile is hung up once its link is up.
    CHECK(ev_conn_request(OTHER, 0x000000) == HCI_EVENT_NOP);
    l2cap_incoming(OTHER, 0x0091, 0x11, 0x55);  // not ours: our slots stay free
    CHECK(s_cid_control == 0);
    const int disc = g_disconnects;
    ev_conn_complete(OTHER, 0x0091, 0);
    CHECK(g_disconnects == disc + 1 && is_link_addr(PC));
    s_connect_watchdog.process(&s_connect_watchdog);
    CHECK(!s_link_claimed);
    s_sync_timer.process(&s_sync_timer);
    CHECK(!syncing());
}

static void pair_incoming(const bd_addr_t a, uint16_t handle)
{
    sync();
    g_have_key = false;
    CHECK(ev_conn_request(a, 0x000000) == HCI_EVENT_NOP);
    ev_conn_complete(a, handle, 0);
    l2cap_incoming(a, handle, 0x11, 0x61);
    l2cap_incoming(a, handle, 0x13, 0x62);
    l2cap_opened(a, handle, 0x11, 0x61);
    l2cap_opened(a, handle, 0x13, 0x62);
    CHECK(connected());
    ev_disconnect(handle, 0x13);
}

static void test_new_console_replaces_old()
{
    static const bd_addr_t SWITCH2 = {0x98, 0xb6, 0xe9, 0x09, 0x09, 0x09};
    pair_incoming(SWITCH, 0x0085);
    const int keys = g_keys_dropped;
    pair_incoming(SWITCH, 0x0086);  // same console again: key kept
    CHECK(g_keys_dropped == keys);
    pair_incoming(SWITCH2, 0x0087);  // another console: the old key goes
    CHECK(g_keys_dropped == keys + 1 && is_console(SWITCH2, 0, false) && !is_console(SWITCH, 0, false));

    // While an unknown host holds the claim, the paired console's channels
    // are refused instead of taking its slots.
    static const bd_addr_t PC = {0x20, 0x20, 0x30, 0x40, 0x50, 0x60};
    sync();
    CHECK(ev_conn_request(PC, 0x000100) == HCI_EVENT_NOP);
    ev_conn_complete(PC, 0x0088, 0);
    const int declines = g_declines;
    l2cap_incoming(SWITCH2, 0x0089, 0x11, 0x56);
    CHECK(g_declines == declines + 1 && s_cid_control == 0 && s_link_handle == 0x0088);
    ev_disconnect(0x0088, 0x13);
    s_sync_timer.process(&s_sync_timer);
}

int main(int argc, char **)
{
    g_verbose = argc > 1;
    RUN(test_begin);
    RUN(test_gamepad_passes_through);
    RUN(test_console_pairs);
    RUN(test_reconnect_as_master);
    RUN(test_watchdog_while_paging);
    RUN(test_late_page_success);
    RUN(test_disconnect_while_paging);
    RUN(test_qos_one_at_a_time);
    RUN(test_pads_relaxed_while_slave);
    RUN(test_pads_never_sniff);
    RUN(test_pad_sniff_and_drop);
    RUN(test_connectable);
    RUN(test_console_master_page);
    RUN(test_console_link_key_stored);
    RUN(test_held_channels);
    RUN(test_unplug_forgets);
    RUN(test_unknown_hosts);
    RUN(test_new_console_replaces_old);
    DONE("bt_hid_device");
}
