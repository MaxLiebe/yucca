#include "yucca/settings.h"

namespace yucca {

int32_t MemorySettings::get_int(const char *key, int32_t fallback) const
{
    auto it = ints_.find(key);
    return it == ints_.end() ? fallback : it->second;
}

void MemorySettings::set_int(const char *key, int32_t value) { ints_[key] = value; }

std::string MemorySettings::get_str(const char *key, const char *fallback) const
{
    auto it = strs_.find(key);
    return it == strs_.end() ? std::string(fallback) : it->second;
}

void MemorySettings::set_str(const char *key, const std::string &value) { strs_[key] = value; }

void MemorySettings::erase(const char *key)
{
    ints_.erase(key);
    strs_.erase(key);
}

}  // namespace yucca
