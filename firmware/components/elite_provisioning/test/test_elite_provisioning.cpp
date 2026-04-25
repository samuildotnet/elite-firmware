// Host-side unit tests for elite_provisioning's NVS parsing layer.
//
// These tests run on the developer's machine (and in CI) without any
// ESP32 in the loop. They exercise ``load_from_reader`` against a stub
// ``NvsReader`` that returns canned values from a ``std::map``. The
// real ESP-IDF code path (``IdfNvsReader``) is excluded by the
// ``USE_ESP_IDF`` guard in elite_provisioning.cpp, so the production
// .cpp can be compiled host-side as a plain translation unit.
//
// Run with:
//   make -C firmware/components/elite_provisioning/test

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

#include "../elite_provisioning.h"

using esphome::elite_provisioning::NvsReader;
using esphome::elite_provisioning::NvsSchema;
using esphome::elite_provisioning::ProvisioningConfig;
using esphome::elite_provisioning::load_from_reader;

namespace {

class StubReader : public NvsReader {
 public:
  std::map<std::string, std::string> values;

  bool read_string(const char *key, std::string &out) override {
    auto it = values.find(key);
    if (it == values.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
};

int failures = 0;
const char *current_test = "<none>";

#define EXPECT(cond)                                                                  \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::fprintf(stderr, "  FAIL [%s] %s:%d  %s\n", current_test, __FILE__,         \
                   __LINE__, #cond);                                                  \
      ++failures;                                                                     \
    }                                                                                 \
  } while (0)

#define EXPECT_EQ(a, b)                                                               \
  do {                                                                                \
    auto _a = (a);                                                                    \
    auto _b = (b);                                                                    \
    if (!(_a == _b)) {                                                                \
      std::fprintf(stderr, "  FAIL [%s] %s:%d  expected %s == %s\n", current_test,    \
                   __FILE__, __LINE__, #a, #b);                                       \
      ++failures;                                                                     \
    }                                                                                 \
  } while (0)

#define RUN(name)         \
  do {                    \
    current_test = #name; \
    std::printf("  %s\n", current_test); \
    name();               \
  } while (0)

void test_full_config_loads_every_key() {
  StubReader r;
  r.values[NvsSchema::KEY_INVERTER_MODEL] = "deye-12k-sg04lp3";
  r.values[NvsSchema::KEY_DEVICE_ID] = "11111111-2222-3333-4444-555555555555";
  r.values[NvsSchema::KEY_DEVICE_TOKEN] = "tok_abcdef0123456789abcdef01";
  r.values[NvsSchema::KEY_MQTT_HOST] = "mqtt.staging.example";
  r.values[NvsSchema::KEY_WIFI_SSID] = "home-2g";
  r.values[NvsSchema::KEY_WIFI_PW] = "wifipw1234";
  r.values[NvsSchema::KEY_SOLARMAN_SN] = "1234567890";

  ProvisioningConfig cfg;
  EXPECT(load_from_reader(r, cfg));
  EXPECT_EQ(cfg.inverter_model, std::string("deye-12k-sg04lp3"));
  EXPECT_EQ(cfg.device_id, std::string("11111111-2222-3333-4444-555555555555"));
  EXPECT_EQ(cfg.device_token, std::string("tok_abcdef0123456789abcdef01"));
  EXPECT_EQ(cfg.mqtt_host, std::string("mqtt.staging.example"));
  EXPECT_EQ(cfg.wifi_ssid, std::string("home-2g"));
  EXPECT_EQ(cfg.wifi_password, std::string("wifipw1234"));
  EXPECT_EQ(cfg.solarman_serial, std::string("1234567890"));
  EXPECT(cfg.is_provisioned());
  EXPECT(cfg.has_wifi());
  EXPECT(cfg.has_solarman());
}

void test_minimal_config_keeps_default_mqtt_host() {
  // Only the required identity pair is present — every other key is
  // missing. mqtt_host should stay at the compile-time default and
  // optional fields should remain empty.
  StubReader r;
  r.values[NvsSchema::KEY_INVERTER_MODEL] = "deye-5k-sg03lp1";
  r.values[NvsSchema::KEY_DEVICE_ID] = "abcd";
  r.values[NvsSchema::KEY_DEVICE_TOKEN] = "tok";

  ProvisioningConfig cfg;
  EXPECT(load_from_reader(r, cfg));
  EXPECT_EQ(cfg.mqtt_host, std::string(NvsSchema::DEFAULT_MQTT_HOST));
  EXPECT(!cfg.has_wifi());
  EXPECT(!cfg.has_solarman());
}

void test_empty_mqtt_host_does_not_overwrite_default() {
  // A backend that wrote ``mqtt_host=""`` (empty) into NVS — perhaps
  // by mistake — should not erase the safe default.
  StubReader r;
  r.values[NvsSchema::KEY_DEVICE_ID] = "id";
  r.values[NvsSchema::KEY_DEVICE_TOKEN] = "tok";
  r.values[NvsSchema::KEY_MQTT_HOST] = "";

  ProvisioningConfig cfg;
  EXPECT(load_from_reader(r, cfg));
  EXPECT_EQ(cfg.mqtt_host, std::string(NvsSchema::DEFAULT_MQTT_HOST));
}

void test_missing_required_fields_returns_unprovisioned() {
  // Brand-new dongle, never touched by /provision: NVS namespace
  // exists but is empty. ``is_provisioned()`` must be false so the
  // MQTT publisher does not try to connect with empty creds.
  StubReader r;
  ProvisioningConfig cfg;
  EXPECT(!load_from_reader(r, cfg));
  EXPECT(!cfg.is_provisioned());
  EXPECT_EQ(cfg.mqtt_host, std::string(NvsSchema::DEFAULT_MQTT_HOST));
}

void test_missing_token_alone_is_unprovisioned() {
  // Edge case from a backend bug: device_id written, token not.
  // Treating this as provisioned would let the dongle try to
  // authenticate to EMQX with an empty password — fail closed.
  StubReader r;
  r.values[NvsSchema::KEY_DEVICE_ID] = "id";

  ProvisioningConfig cfg;
  EXPECT(!load_from_reader(r, cfg));
  EXPECT(!cfg.is_provisioned());
}

void test_wifi_optional_solarman_optional_independence() {
  // A customer with our SIM-only LTE backhaul wouldn't bake in WiFi
  // creds; an installer who skips Solarman migration wouldn't bake
  // in the LSW3 SN. Either combination must be valid.
  for (bool with_wifi : {false, true}) {
    for (bool with_solarman : {false, true}) {
      StubReader r;
      r.values[NvsSchema::KEY_DEVICE_ID] = "id";
      r.values[NvsSchema::KEY_DEVICE_TOKEN] = "tok";
      if (with_wifi) {
        r.values[NvsSchema::KEY_WIFI_SSID] = "ssid";
        r.values[NvsSchema::KEY_WIFI_PW] = "pw";
      }
      if (with_solarman) {
        r.values[NvsSchema::KEY_SOLARMAN_SN] = "1111111111";
      }

      ProvisioningConfig cfg;
      EXPECT(load_from_reader(r, cfg));
      EXPECT_EQ(cfg.has_wifi(), with_wifi);
      EXPECT_EQ(cfg.has_solarman(), with_solarman);
    }
  }
}

}  // namespace

int main() {
  std::printf("elite_provisioning host tests\n");
  RUN(test_full_config_loads_every_key);
  RUN(test_minimal_config_keeps_default_mqtt_host);
  RUN(test_empty_mqtt_host_does_not_overwrite_default);
  RUN(test_missing_required_fields_returns_unprovisioned);
  RUN(test_missing_token_alone_is_unprovisioned);
  RUN(test_wifi_optional_solarman_optional_independence);

  if (failures == 0) {
    std::printf("OK\n");
    return 0;
  }
  std::fprintf(stderr, "%d failure(s)\n", failures);
  return 1;
}
