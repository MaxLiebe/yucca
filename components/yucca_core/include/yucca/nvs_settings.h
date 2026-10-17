// Settings in the ESP32's NVS flash (namespace "yucca"). Device only.
#pragma once

#include <atomic>
#include <cstdint>

#include "yucca/settings.h"

namespace yucca {

class NvsSettings : public Settings {
public:
    // Opens the namespace; initialises NVS flash if nobody has yet.
    NvsSettings();
    ~NvsSettings() override;

    int32_t get_int(const char *key, int32_t fallback) const override;
    void set_int(const char *key, int32_t value) override;
    std::string get_str(const char *key, const char *fallback) const override;
    void set_str(const char *key, const std::string &value) override;
    void erase(const char *key) override;
    void commit() override;

private:
    uint32_t handle_ = 0;
    std::atomic<bool> dirty_{false};  // set from the app tick and the console task
};

}  // namespace yucca
