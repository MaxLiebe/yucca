#include "yucca/settings_schema.h"

#include <cstdlib>
#include <cstring>

#include "yucca/settings.h"

namespace yucca {

static const SettingChoice kFaceLayouts[] = {
    {"0", "By position (Nintendo layout)"},
    {"1", "By label (Xbox-style A/B/X/Y)"},
};

static const SettingChoice kNoTargets[] = {{"wii", "Wii"}};

static SettingSpec s_schema[] = {
    {keys::kTarget, "Console", SettingType::Choice, 0, 0, 0, kNoTargets, 1, "wii", true},
    {keys::kFaceLayout, "Face buttons", SettingType::Choice, 0, 0, 0, kFaceLayouts, 2, "0", true},
    {keys::kBle, "Look for Bluetooth LE controllers (Stadia, Xbox)", SettingType::Int, 0, 1, 1, nullptr, 0, nullptr,
     true},
    {keys::kRumble, "Rumble strength (%)", SettingType::Int, 0, 100, 60, nullptr, 0, nullptr, false},
    {"wii_mirror", "Wii: also press Wii Remote buttons", SettingType::Int, 0, 1, 1, nullptr, 0, nullptr, true},
    {"wiiu_mode", "Wii U: be a Wii Remote + Classic Controller instead of a Pro Controller", SettingType::Int, 0, 1, 0,
     nullptr, 0, nullptr, true},
};

void schema_init(const SettingChoice *targets, uint8_t count)
{
    if (count > 0) {
        s_schema[0].choices = targets;
        s_schema[0].choice_count = count;
        s_schema[0].def_choice = targets[0].value;
    }
}

const SettingSpec *schema(size_t *count)
{
    *count = sizeof(s_schema) / sizeof(s_schema[0]);
    return s_schema;
}

const SettingSpec *schema_find(const char *key)
{
    for (auto &spec : s_schema) {
        if (std::strcmp(spec.key, key) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

// Choices are stored as strings, except face_layout which components read as int.
static bool choice_is_int(const SettingSpec &spec) { return std::strcmp(spec.key, keys::kFaceLayout) == 0; }

const char *schema_set(Settings &settings, const char *key, const char *value, bool *restart)
{
    const SettingSpec *spec = schema_find(key);
    if (spec == nullptr) {
        return "unknown setting";
    }
    if (spec->type == SettingType::Int) {
        char *end = nullptr;
        long v = std::strtol(value, &end, 10);
        if (end == value || *end != '\0') {
            return "not a number";
        }
        if (v < spec->min || v > spec->max) {
            return "out of range";
        }
        settings.set_int(key, static_cast<int32_t>(v));
    } else {
        bool ok = false;
        for (uint8_t i = 0; i < spec->choice_count; i++) {
            ok = ok || std::strcmp(spec->choices[i].value, value) == 0;
        }
        if (!ok) {
            return "not one of the choices";
        }
        if (choice_is_int(*spec)) {
            settings.set_int(key, std::atoi(value));
        } else {
            settings.set_str(key, value);
        }
    }
    settings.commit();
    if (restart != nullptr) {
        *restart = spec->restart;
    }
    return nullptr;
}

std::string schema_get(const Settings &settings, const SettingSpec &spec)
{
    if (spec.type == SettingType::Int) {
        return std::to_string(settings.get_int(spec.key, spec.def));
    }
    if (choice_is_int(spec)) {
        return std::to_string(settings.get_int(spec.key, std::atoi(spec.def_choice)));
    }
    return settings.get_str(spec.key, spec.def_choice);
}

// Labels and values are ours (no quotes or backslashes), so no escaping needed.
std::string schema_json(const Settings &settings)
{
    std::string out = "{";
    bool first = true;
    for (const auto &spec : s_schema) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += "\"" + std::string(spec.key) + "\":{\"label\":\"" + spec.label + "\",";
        if (spec.type == SettingType::Int) {
            out += "\"type\":\"int\",\"min\":" + std::to_string(spec.min) + ",\"max\":" + std::to_string(spec.max);
        } else {
            out += "\"type\":\"choice\",\"choices\":[";
            for (uint8_t i = 0; i < spec.choice_count; i++) {
                if (i) {
                    out += ",";
                }
                out += "{\"value\":\"" + std::string(spec.choices[i].value) + "\",\"label\":\"" +
                       spec.choices[i].label + "\"}";
            }
            out += "]";
        }
        out += ",\"restart\":" + std::string(spec.restart ? "true" : "false");
        out += ",\"value\":\"" + schema_get(settings, spec) + "\"}";
    }
    out += "}";
    return out;
}

}  // namespace yucca
