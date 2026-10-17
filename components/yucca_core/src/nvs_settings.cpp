#include "yucca/nvs_settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace yucca {

static const char *TAG = "settings";

NvsSettings::NvsSettings()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("yucca", NVS_READWRITE, &h));
    handle_ = h;
}

NvsSettings::~NvsSettings() { nvs_close(handle_); }

int32_t NvsSettings::get_int(const char *key, int32_t fallback) const
{
    int32_t v;
    return nvs_get_i32(handle_, key, &v) == ESP_OK ? v : fallback;
}

void NvsSettings::set_int(const char *key, int32_t value)
{
    if (get_int(key, value ^ 1) == value) {
        return;
    }
    if (nvs_set_i32(handle_, key, value) == ESP_OK) {
        dirty_ = true;
    } else {
        ESP_LOGE(TAG, "Could not store %s", key);
    }
}

std::string NvsSettings::get_str(const char *key, const char *fallback) const
{
    size_t len = 0;
    if (nvs_get_str(handle_, key, nullptr, &len) != ESP_OK || len == 0) {
        return fallback;
    }
    std::string s(len, '\0');
    if (nvs_get_str(handle_, key, s.data(), &len) != ESP_OK) {
        return fallback;
    }
    s.resize(len - 1);  // drop the terminator
    return s;
}

void NvsSettings::set_str(const char *key, const std::string &value)
{
    if (nvs_set_str(handle_, key, value.c_str()) == ESP_OK) {
        dirty_ = true;
    } else {
        ESP_LOGE(TAG, "Could not store %s", key);
    }
}

void NvsSettings::erase(const char *key)
{
    if (nvs_erase_key(handle_, key) == ESP_OK) {
        dirty_ = true;
    }
}

void NvsSettings::commit()
{
    // Clear first: a set racing with this commit leaves dirty_ set again.
    if (dirty_.exchange(false) && nvs_commit(handle_) != ESP_OK) {
        dirty_ = true;
    }
}

}  // namespace yucca
