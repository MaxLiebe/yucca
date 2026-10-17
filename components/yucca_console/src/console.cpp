#include "yucca/console.h"

#include <cstdio>
#include <string>

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "yucca/app.h"
#include "yucca/settings.h"
#include "yucca/settings_schema.h"

namespace yucca {

namespace {
Settings *s_settings;
App *s_app;
InputDriver *s_input;
Target *s_target;

// Console commands run on the console task; the BTstack-side state they read
// is plain flags, and the actions they trigger are posted to the run loop by
// the components themselves where needed.
int cmd_info(int, char **)
{
    std::string out = "YUCCA {\"fw\":\"";
    out += esp_app_get_description()->version;
    out += "\",\"target\":\"";
    out += s_target->id();
    out += "\",\"controller\":";
    out += s_input->ready() ? "true" : "false";
    out += ",\"console\":";
    out += s_target->connected() ? "true" : "false";
    out += ",\"paired\":";
    out += s_target->has_paired_console() ? "true" : "false";
    out += ",\"settings\":";
    out += schema_json(*s_settings);
    out += "}";
    std::printf("%s\n", out.c_str());
    return 0;
}

int cmd_set(int argc, char **argv)
{
    if (argc != 3) {
        std::printf("ERR usage: yucca_set <key> <value>\n");
        return 1;
    }
    bool restart = false;
    const char *err = schema_set(*s_settings, argv[1], argv[2], &restart);
    if (err != nullptr) {
        std::printf("ERR %s\n", err);
        return 1;
    }
    if (restart) {
        std::printf("OK restarting\n");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    std::printf("OK\n");
    return 0;
}

int cmd_sync(int, char **)
{
    s_app->request_sync();
    std::printf("OK\n");
    return 0;
}

int cmd_forget(int, char **)
{
    s_app->request_forget();
    std::printf("OK\n");
    return 0;
}

void add(const char *name, const char *help, esp_console_cmd_func_t fn)
{
    esp_console_cmd_t cmd = {};
    cmd.command = name;
    cmd.help = help;
    cmd.func = fn;
    esp_console_cmd_register(&cmd);
}
}  // namespace

void console_init(Settings &settings, App &app, InputDriver &input, Target &target)
{
    s_settings = &settings;
    s_app = &app;
    s_input = &input;
    s_target = &target;
}

void console_register_commands()
{
    if (s_settings == nullptr) {
        return;
    }
    add("yucca_info", "YUCCA status and settings as one JSON line", cmd_info);
    add("yucca_set", "Change a setting: yucca_set <key> <value>", cmd_set);
    add("yucca_sync", "Open the console sync window", cmd_sync);
    add("yucca_forget", "Forget the console and all controllers", cmd_forget);
}

}  // namespace yucca
