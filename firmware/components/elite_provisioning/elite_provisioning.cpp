#include "elite_provisioning.h"

#ifdef USE_ESP_IDF
#include "nvs.h"
#include "nvs_flash.h"
#endif

namespace esphome {
namespace elite_provisioning {

static const char *const TAG = "elite_provisioning";

bool load_from_reader(NvsReader &reader, ProvisioningConfig &cfg) {
  reader.read_string(NvsSchema::KEY_INVERTER_MODEL, cfg.inverter_model);
  reader.read_string(NvsSchema::KEY_DEVICE_ID, cfg.device_id);
  reader.read_string(NvsSchema::KEY_DEVICE_TOKEN, cfg.device_token);

  // mqtt_host has a non-empty default; only overwrite when NVS really
  // has a value, so a wiped namespace still publishes to our prod.
  std::string host_buf;
  if (reader.read_string(NvsSchema::KEY_MQTT_HOST, host_buf) && !host_buf.empty()) {
    cfg.mqtt_host = host_buf;
  }

  reader.read_string(NvsSchema::KEY_WIFI_SSID, cfg.wifi_ssid);
  reader.read_string(NvsSchema::KEY_WIFI_PW, cfg.wifi_password);
  reader.read_string(NvsSchema::KEY_SOLARMAN_SN, cfg.solarman_serial);

  return cfg.is_provisioned();
}

#ifdef USE_ESP_IDF

namespace {

/// Production NVS reader. Opens the ``elite-cfg`` namespace once at
/// construction and reuses the handle for every key. ``read_string``
/// returns ``false`` on any error so the consumer can keep its
/// default in place.
class IdfNvsReader : public NvsReader {
 public:
  IdfNvsReader() {
    esp_err_t err = nvs_open(NvsSchema::NAMESPACE, NVS_READONLY, &handle_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "nvs_open(%s) failed: %s — treating namespace as empty",
               NvsSchema::NAMESPACE, esp_err_to_name(err));
      handle_ = 0;
      open_ = false;
    } else {
      open_ = true;
    }
  }

  ~IdfNvsReader() override {
    if (open_) {
      nvs_close(handle_);
    }
  }

  bool read_string(const char *key, std::string &out) override {
    if (!open_) {
      return false;
    }
    size_t required = 0;
    esp_err_t err = nvs_get_str(handle_, key, nullptr, &required);
    if (err != ESP_OK || required == 0) {
      return false;
    }
    std::string buf(required, '\0');
    err = nvs_get_str(handle_, key, buf.data(), &required);
    if (err != ESP_OK) {
      return false;
    }
    // ``required`` includes the trailing NUL; trim it from std::string.
    if (!buf.empty() && buf.back() == '\0') {
      buf.pop_back();
    }
    out = std::move(buf);
    return true;
  }

 protected:
  nvs_handle_t handle_{0};
  bool open_{false};
};

}  // namespace

void EliteProvisioning::setup() {
  // ESPHome already calls ``nvs_flash_init`` early in main; calling it
  // again here is a defensive no-op (returns ESP_ERR_NVS_NO_FREE_PAGES
  // / ESP_ERR_NVS_NEW_VERSION_FOUND only on a corrupt partition).
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition unusable (%s); erasing", esp_err_to_name(err));
    nvs_flash_erase();
    nvs_flash_init();
  }

  IdfNvsReader reader;
  load_from_reader(reader, cfg_);

  if (!cfg_.is_provisioned()) {
    ESP_LOGE(TAG,
             "elite-cfg namespace missing device_id/device_token — dongle is "
             "factory-fresh; flash with the web flasher to provision");
    // Don't mark as failed — the firmware should still be reachable
    // via the captive portal AP for diagnostics. MQTT/Solarman
    // publishers gate on is_provisioned() and quietly disable.
  }
}

void EliteProvisioning::dump_config() {
  ESP_LOGCONFIG(TAG, "Elite Provisioning:");
  ESP_LOGCONFIG(TAG, "  NVS namespace: %s", NvsSchema::NAMESPACE);
  ESP_LOGCONFIG(TAG, "  Inverter model: %s",
                cfg_.inverter_model.empty() ? "(unset)" : cfg_.inverter_model.c_str());
  ESP_LOGCONFIG(TAG, "  Device ID: %s",
                cfg_.device_id.empty() ? "(unset)" : cfg_.device_id.c_str());
  // Never log the token. Length-only signal is enough to tell whether
  // provisioning was completed.
  ESP_LOGCONFIG(TAG, "  Device token: %s",
                cfg_.device_token.empty() ? "(unset)" : "(redacted)");
  ESP_LOGCONFIG(TAG, "  MQTT host: %s", cfg_.mqtt_host.c_str());
  ESP_LOGCONFIG(TAG, "  WiFi SSID: %s",
                cfg_.wifi_ssid.empty() ? "(unset — AP fallback)" : cfg_.wifi_ssid.c_str());
  ESP_LOGCONFIG(TAG, "  WiFi password: %s",
                cfg_.wifi_password.empty() ? "(unset)" : "(redacted)");
  ESP_LOGCONFIG(TAG, "  Solarman SN: %s",
                cfg_.solarman_serial.empty() ? "(unset — disabled)" : cfg_.solarman_serial.c_str());
  ESP_LOGCONFIG(TAG, "  Provisioned: %s", cfg_.is_provisioned() ? "yes" : "NO");
}

#else  // USE_ESP_IDF

void EliteProvisioning::setup() {}
void EliteProvisioning::dump_config() {}

#endif  // USE_ESP_IDF

}  // namespace elite_provisioning
}  // namespace esphome
