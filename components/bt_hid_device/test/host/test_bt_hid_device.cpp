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
bool hci_can_send_command_packet_now(void) { return true; }
static std::vector<const hci_cmd_t *> g_cmds;
uint8_t hci_send_cmd(const hci_cmd_t *cmd, ...)
{
    g_cmds.push_back(cmd);
    return 0;
}
const hci_cmd_t hci_disconnect = {};
const hci_cmd_t hci_write_link_policy_settings = {};
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
uint8_t gap_request_role(const bd_addr_t, hci_role_t) { g_role_requests++; return 0; }
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
    CHECK(de_get_len(s_hid_record) < sizeof(s_hid_record));
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
    g_can_send = true;

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

static void test_watchdog_and_forget()
{
    connect();
    CHECK(connecting());
    g_cmds.clear();
    s_connect_watchdog.process(&s_connect_watchdog);
    CHECK(!connecting());
    CHECK(!g_cmds.empty() && g_cmds.back() == &hci_create_connection_cancel);

    // Gamepad tuning.
    g_cmds.clear();
    tune_link(0x0050, PAD);
    CHECK(g_role_requests == 1);
    CHECK(g_cmds.size() == 2);

    forget();
    CHECK(!has_host() && !g_tlv_have && g_keys_dropped == 1);
    connect();
    CHECK(!connecting());  // nothing to page
}

int main(int argc, char **)
{
    g_verbose = argc > 1;
    RUN(test_begin);
    RUN(test_gamepad_passes_through);
    RUN(test_console_pairs);
    RUN(test_reconnect_as_master);
    RUN(test_watchdog_and_forget);
    DONE("bt_hid_device");
}
