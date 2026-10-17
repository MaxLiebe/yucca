// YUCCA entry point: settings, Bluetooth, one input, one target.

#include <cstring>
#include <string>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "yucca/app.h"
#include "yucca/bluepad32_input.h"
#include "yucca/console.h"
#include "yucca/nvs_settings.h"
#include "yucca/settings_schema.h"
#include "yucca/switch_target.h"
#include "yucca/wii_target.h"

// BOOT button on most dev boards. Short press: sync. Hold 5 s: forget all.
static constexpr gpio_num_t kBootGpio = GPIO_NUM_0;
static constexpr uint32_t kTickMs = 20;

// Consoles built into this firmware. The order is the console menu's (player
// LED 1, 2, 3, ...); the first is the default.
struct TargetEntry {
    yucca::SettingChoice choice;
    yucca::Target &(*get)();
};
static yucca::Target &wii() { return yucca::WiiTarget::instance(); }
static yucca::Target &wiiu()
{
    auto &t = yucca::WiiTarget::instance();
    t.set_model(yucca::WiiTarget::Model::WiiU);
    return t;
}
static yucca::Target &nswitch() { return yucca::SwitchTarget::instance(); }
static yucca::Target &switch2()
{
    auto &t = yucca::SwitchTarget::instance();
    t.set_model(yucca::SwitchTarget::Model::Switch2);
    return t;
}
static const TargetEntry kTargets[] = {
    {{"wii", "Wii"}, wii},
    {{"wiiu", "Wii U"}, wiiu},
    {{"switch", "Switch"}, nswitch},
    {{"switch2", "Switch 2"}, switch2},
};
static constexpr uint8_t kTargetCount = sizeof(kTargets) / sizeof(kTargets[0]);
static yucca::SettingChoice s_choices[kTargetCount];

static yucca::App *s_app;
static btstack_timer_source_t s_tick;

static void tick(btstack_timer_source_t *ts)
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    s_app->tick(now_ms, gpio_get_level(kBootGpio) == 0);
    btstack_run_loop_set_timer(ts, kTickMs);
    btstack_run_loop_add_timer(ts);
}

static yucca::Target &select_target(const std::string &id)
{
    for (const auto &t : kTargets) {
        if (id == t.choice.value) {
            return t.get();
        }
    }
    return kTargets[0].get();
}

static void restart() { esp_restart(); }

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

    for (uint8_t i = 0; i < kTargetCount; i++) {
        s_choices[i] = kTargets[i].choice;
    }
    yucca::schema_init(s_choices, kTargetCount);

    static yucca::NvsSettings settings;
    auto &input = yucca::Bluepad32Input::instance();
    input.set_ble_enabled(settings.get_int(yucca::keys::kBle, 1) != 0);

    // The target sees every Bluetooth event first and hands the rest to Bluepad32.
    yucca::Target &target = select_target(settings.get_str(yucca::keys::kTarget, kTargets[0].choice.value));
    target.set_fallback_handler(yucca::Bluepad32Input::packet_handler());

    static yucca::App app(settings, input, target, s_choices, kTargetCount, restart);
    s_app = &app;
    yucca::console_init(settings, app, input, target);
    input.set_console_hook(yucca::console_register_commands);
    app.begin();

    btstack_run_loop_set_timer_handler(&s_tick, tick);
    btstack_run_loop_set_timer(&s_tick, kTickMs);
    btstack_run_loop_add_timer(&s_tick);

    // Does not return.
    btstack_run_loop_execute();
}
