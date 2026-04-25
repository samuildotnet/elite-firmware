#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elite_provisioning {

/// NVS namespace + key catalogue. Kept as a single source of truth so
/// the backend NVS-blob generator (``backend/src/app/services/nvs.py``
/// in ``elite-energy``) and the firmware here cannot drift.
struct NvsSchema {
  static constexpr const char *NAMESPACE = "elite-cfg";

  static constexpr const char *KEY_INVERTER_MODEL = "inverter_model";
  static constexpr const char *KEY_DEVICE_ID = "device_id";
  static constexpr const char *KEY_DEVICE_TOKEN = "device_token";
  static constexpr const char *KEY_MQTT_HOST = "mqtt_host";
  static constexpr const char *KEY_WIFI_SSID = "wifi_ssid";
  static constexpr const char *KEY_WIFI_PW = "wifi_pw";
  static constexpr const char *KEY_SOLARMAN_SN = "solarman_sn";

  /// Default broker hostname when ``mqtt_host`` is not present in NVS.
  /// Lives here so the firmware fails closed onto our prod ingest
  /// rather than an empty string.
  static constexpr const char *DEFAULT_MQTT_HOST = "mqtt.elite.prygoda.xyz";
};

/// Snapshot of the ``elite-cfg`` NVS namespace.
///
/// Held by the ``EliteProvisioning`` component and exposed read-only via
/// accessor methods. ``device_id`` / ``device_token`` empty after
/// ``load()`` ⇒ the dongle is unprovisioned (factory-fresh) and other
/// publishers must short-circuit.
struct ProvisioningConfig {
  std::string inverter_model;
  std::string device_id;
  std::string device_token;
  std::string mqtt_host{NvsSchema::DEFAULT_MQTT_HOST};
  std::string wifi_ssid;
  std::string wifi_password;
  std::string solarman_serial;

  /// True iff both required identity fields are present. The MQTT and
  /// Solarman publishers gate on this.
  bool is_provisioned() const { return !device_id.empty() && !device_token.empty(); }

  /// True iff WiFi STA creds are baked in. When false, the firmware
  /// keeps the captive-portal AP up so the installer can enter creds
  /// from a phone.
  bool has_wifi() const { return !wifi_ssid.empty(); }

  /// True iff Solarman SN is baked in. When false, the Solarman V5
  /// publisher is disabled (saves RAM + skips a TCP connection).
  bool has_solarman() const { return !solarman_serial.empty(); }
};

/// Pluggable NVS reader so we can unit-test the parsing layer on the
/// host without an ESP32 in the loop. Tests inject a stub that returns
/// pre-seeded values; production uses ``IdfNvsReader`` which calls
/// ``nvs_get_str`` against the ``elite-cfg`` namespace.
class NvsReader {
 public:
  virtual ~NvsReader() = default;

  /// Look up ``key`` in the namespace. Writes into ``out`` and returns
  /// ``true`` if found, ``false`` otherwise. Buffer overflows are
  /// truncated silently (NVS values are small fixed-shape strings).
  virtual bool read_string(const char *key, std::string &out) = 0;
};

/// Load values from a reader into ``cfg``. Returns ``true`` if both
/// required fields (``device_id``, ``device_token``) were present, even
/// if optional fields were missing. Defaults that the firmware cares
/// about (mqtt_host) are seeded into ``cfg`` before calling the reader,
/// so any read failure leaves the default in place.
bool load_from_reader(NvsReader &reader, ProvisioningConfig &cfg);

class EliteProvisioning : public Component {
 public:
  void setup() override;
  void dump_config() override;
  /// Run before WiFi / network so downstream components see populated
  /// values in their own ``setup()``.
  float get_setup_priority() const override { return setup_priority::BUS + 1.0f; }

  bool is_provisioned() const { return cfg_.is_provisioned(); }
  bool has_wifi() const { return cfg_.has_wifi(); }
  bool has_solarman() const { return cfg_.has_solarman(); }

  const std::string &inverter_model() const { return cfg_.inverter_model; }
  const std::string &device_id() const { return cfg_.device_id; }
  const std::string &device_token() const { return cfg_.device_token; }
  const std::string &mqtt_host() const { return cfg_.mqtt_host; }
  const std::string &wifi_ssid() const { return cfg_.wifi_ssid; }
  const std::string &wifi_password() const { return cfg_.wifi_password; }
  const std::string &solarman_serial() const { return cfg_.solarman_serial; }

  /// Test-only: seed values without going through NVS. Production code
  /// goes through ``setup()`` → ``IdfNvsReader``.
  void set_config_for_test(const ProvisioningConfig &cfg) { cfg_ = cfg; }

 protected:
  ProvisioningConfig cfg_;
};

}  // namespace elite_provisioning
}  // namespace esphome
