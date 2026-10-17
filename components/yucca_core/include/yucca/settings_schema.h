// Every user setting, in one table: the console commands validate against it
// and the web settings page builds its form from it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace yucca {

class Settings;

enum class SettingType : uint8_t { Int, Choice };

struct SettingChoice {
    const char *value;
    const char *label;
};

struct SettingSpec {
    const char *key;
    const char *label;
    SettingType type;
    int32_t min, max, def;              // Int
    const SettingChoice *choices;        // Choice (stored as a string)
    uint8_t choice_count;
    const char *def_choice;
    bool restart;                        // takes effect after a restart
};

// The schema. `targets` fills the `target` choice list with the consoles this
// firmware was built with.
void schema_init(const SettingChoice *targets, uint8_t count);
const SettingSpec *schema(size_t *count);
const SettingSpec *schema_find(const char *key);

// Validate and store `value` for `key`. Returns nullptr on success, else a
// short error message. On success, `*restart` (if given) says whether the
// device must restart for the value to take effect.
const char *schema_set(Settings &settings, const char *key, const char *value, bool *restart = nullptr);

// Current value of a setting as text (its default when unset).
std::string schema_get(const Settings &settings, const SettingSpec &spec);

// {"key":{"label":..,"type":..,...,"value":..},...} for yucca_info.
std::string schema_json(const Settings &settings);

}  // namespace yucca
