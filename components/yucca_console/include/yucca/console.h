// Serial console commands for the web settings page (and power users):
//   yucca_info              one line: YUCCA {json}
//   yucca_set <key> <value> OK | ERR <reason>; restarts for settings that need it
//   yucca_sync / yucca_forget
// Registered into Bluepad32's console (same UART, 115200).
#pragma once

namespace yucca {

class App;
class InputDriver;
class Settings;
class Target;

void console_init(Settings &settings, App &app, InputDriver &input, Target &target);

// Called from Bluepad32 when its console starts.
void console_register_commands();

}  // namespace yucca
