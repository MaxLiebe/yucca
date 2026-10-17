// YUCCA entry point. Placeholder until the Phase 0 port lands (see AGENTS.md):
// brings up BTstack and Bluepad32 and logs gamepads.

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <uni.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "yucca";

static void platform_init(int, const char **) {}
static void platform_on_init_complete(void)
{
    uni_bt_start_scanning_and_autoconnect_unsafe();
}
static uni_error_t platform_on_device_discovered(bd_addr_t, const char *, uint16_t, uint8_t)
{
    return UNI_ERROR_SUCCESS;
}
static void platform_on_device_connected(uni_hid_device_t *) {}
static void platform_on_device_disconnected(uni_hid_device_t *) {}
static uni_error_t platform_on_device_ready(uni_hid_device_t *)
{
    ESP_LOGI(TAG, "Gamepad ready");
    return UNI_ERROR_SUCCESS;
}
static void platform_on_oob_event(uni_platform_oob_event_t, void *) {}
static void platform_on_controller_data(uni_hid_device_t *, uni_controller_t *) {}
static const uni_property_t *platform_get_property(uni_property_idx_t)
{
    return nullptr;
}

static struct uni_platform s_platform = {
    .name = "YUCCA",
    .init = platform_init,
    .on_init_complete = platform_on_init_complete,
    .on_device_discovered = platform_on_device_discovered,
    .on_device_connected = platform_on_device_connected,
    .on_device_disconnected = platform_on_device_disconnected,
    .on_device_ready = platform_on_device_ready,
    .on_gamepad_data = nullptr,
    .on_controller_data = platform_on_controller_data,
    .get_property = platform_get_property,
    .on_oob_event = platform_on_oob_event,
    .device_dump = nullptr,
    .register_console_cmds = nullptr,
};

extern "C" void app_main(void)
{
#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif
    ESP_LOGI(TAG, "YUCCA starting");
    btstack_init();
    uni_platform_set_custom(&s_platform);
    uni_init(0, nullptr);
    btstack_run_loop_execute();
}
