// YUCCA entry point: settings, Bluetooth, one input, one target.

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "yucca/app.h"
#include "yucca/bluepad32_input.h"
#include "yucca/nvs_settings.h"
#include "yucca/wii_target.h"

// BOOT button on most dev boards. Short press: sync. Hold 5 s: forget all.
static constexpr gpio_num_t kBootGpio = GPIO_NUM_0;
static constexpr uint32_t kTickMs = 20;

static yucca::App *s_app;
static btstack_timer_source_t s_tick;

static void tick(btstack_timer_source_t *ts)
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    s_app->tick(now_ms, gpio_get_level(kBootGpio) == 0);
    btstack_run_loop_set_timer(ts, kTickMs);
    btstack_run_loop_add_timer(ts);
}

extern "C" void app_main(void)
{
#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << kBootGpio;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // BTstack with the ESP32 VHCI controller (also sets up NVS + TLV).
    btstack_init();

    static yucca::NvsSettings settings;
    auto &input = yucca::Bluepad32Input::instance();
    input.set_ble_enabled(settings.get_int(yucca::keys::kBle, 1) != 0);

    // The target sees every Bluetooth event first and hands the rest to Bluepad32.
    yucca::Target &target = yucca::WiiTarget::instance();
    target.set_fallback_handler(yucca::Bluepad32Input::packet_handler());

    static yucca::App app(settings, input, target);
    s_app = &app;
    app.begin();

    btstack_run_loop_set_timer_handler(&s_tick, tick);
    btstack_run_loop_set_timer(&s_tick, kTickMs);
    btstack_run_loop_add_timer(&s_tick);

    // Does not return.
    btstack_run_loop_execute();
}
