#include "fake_wiimote.h"

#include "bluetooth.h"  // hci_con_handle_t, bd_addr_t for wiimote_btstack.h
#include "wiimote_btstack.h"

FakeWiimote g_wm;
int g_verbose = 0;

void fake_reset() { g_wm = FakeWiimote(); }

extern "C" {
esp_err_t wiimote_init(const wiimote_config_t *config)
{
    g_wm.callbacks = config->callbacks;
    return ESP_OK;
}
void wiimote_btstack_set_fallback_handler(btstack_packet_handler_t) {}
void wiimote_btstack_tune_link(hci_con_handle_t, const bd_addr_t) { g_wm.tune_calls++; }
esp_err_t wiimote_sync(void)
{
    g_wm.sync_calls++;
    return ESP_OK;
}
esp_err_t wiimote_connect(void)
{
    g_wm.connect_calls++;
    g_wm.connecting = true;
    return ESP_OK;
}
bool wiimote_is_connecting(void) { return g_wm.connecting; }
esp_err_t wiimote_disconnect(void)
{
    g_wm.disconnect_calls++;
    return ESP_OK;
}
bool wiimote_has_paired_host(void) { return g_wm.paired; }
esp_err_t wiimote_forget_host(void)
{
    g_wm.forget_calls++;
    g_wm.paired = false;
    return ESP_OK;
}
void wiimote_send_now(void) { g_wm.send_calls++; }
bool wiimote_link_is_master(void) { return g_wm.master; }
uint8_t wiimote_last_disconnect_reason(void) { return g_wm.reason; }
esp_err_t wiimote_set_button(wiimote_button_t button, bool pressed)
{
    g_wm.buttons[button] = pressed;
    return ESP_OK;
}
esp_err_t wiimote_set_axis(wiimote_axis_t axis, float value)
{
    g_wm.axes[axis] = value;
    return ESP_OK;
}
esp_err_t wiimote_set_battery(uint8_t level)
{
    g_wm.battery = level;
    return ESP_OK;
}
bool wiimote_is_connected(void) { return g_wm.connected; }
}
