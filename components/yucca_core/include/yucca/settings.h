// Key/value settings. NVS on the device, in memory in host tests.
// Keys are at most 15 characters (NVS limit).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace yucca {

class Settings {
public:
    virtual ~Settings() = default;
    virtual int32_t get_int(const char *key, int32_t fallback) const = 0;
    virtual void set_int(const char *key, int32_t value) = 0;
    virtual std::string get_str(const char *key, const char *fallback) const = 0;
    virtual void set_str(const char *key, const std::string &value) = 0;
    virtual void erase(const char *key) = 0;
    // Persist pending writes. Cheap to call when nothing changed.
    virtual void commit() = 0;
};

class MemorySettings : public Settings {
public:
    int32_t get_int(const char *key, int32_t fallback) const override;
    void set_int(const char *key, int32_t value) override;
    std::string get_str(const char *key, const char *fallback) const override;
    void set_str(const char *key, const std::string &value) override;
    void erase(const char *key) override;
    void commit() override { commits_++; }
    int commits() const { return commits_; }

private:
    std::map<std::string, int32_t> ints_;
    std::map<std::string, std::string> strs_;
    int commits_ = 0;
};

// Setting keys used across components.
namespace keys {
constexpr const char *kTarget = "target";        // str: target id, e.g. "wii"
constexpr const char *kFaceLayout = "face_layout"; // int: FaceLayout
constexpr const char *kBle = "ble";              // int: 1 = look for BLE controllers
}  // namespace keys

}  // namespace yucca
