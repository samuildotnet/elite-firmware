#ifdef USE_ESP_IDF

#include "elite_partner_mqtt.h"

#include <cinttypes>
#include <cstdio>
#include <string>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace elite_partner_mqtt {

static const char *const TAG = "elite_partner_mqtt";

namespace {

/// Slugify an ESPHome object_id for use in an MQTT topic segment.
/// We accept a-z 0-9 _ - and replace anything else with '_'.
std::string slug(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out += c;
    } else if (c >= 'A' && c <= 'Z') {
      out += static_cast<char>(c - 'A' + 'a');
    } else {
      out += '_';
    }
  }
  return out;
}

}  // namespace

void ElitePartnerMqtt::setup() {
  start_client_();
  hook_sensor_callbacks_();
}

void ElitePartnerMqtt::loop() {
  const uint32_t now = millis();
  if (now - last_heartbeat_ >= publish_interval_ms_) {
    last_heartbeat_ = now;
    heartbeat_();
  }
}

void ElitePartnerMqtt::dump_config() {
  ESP_LOGCONFIG(TAG, "Elite Partner MQTT:");
  ESP_LOGCONFIG(TAG, "  Broker:           %s:%u", broker_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Username:         %s", username_.empty() ? "(anonymous)" : username_.c_str());
  ESP_LOGCONFIG(TAG, "  Client ID:        %s", client_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Topic prefix:     %s", topic_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Heartbeat:        every %" PRIu32 " ms", publish_interval_ms_);
}

void ElitePartnerMqtt::start_client_() {
  // Build a `mqtts://...` URI. esp-mqtt picks TLS based on scheme.
  std::string uri = "mqtts://" + broker_ + ":" + std::to_string(port_);

  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = uri.c_str();
  if (!client_id_.empty())
    cfg.credentials.client_id = client_id_.c_str();
  if (!username_.empty())
    cfg.credentials.username = username_.c_str();
  if (!password_.empty())
    cfg.credentials.authentication.password = password_.c_str();

  // Trust any certificate offered by the broker. Production deployments
  // should pin a SHA-256 fingerprint via cfg.broker.verification.
  cfg.broker.verification.skip_cert_common_name_check = true;
  cfg.broker.verification.use_global_ca_store = false;
  cfg.broker.verification.crt_bundle_attach = nullptr;

  cfg.network.timeout_ms = 10000;
  cfg.network.reconnect_timeout_ms = 5000;
  cfg.session.keepalive = 30;

  client_ = esp_mqtt_client_init(&cfg);
  if (client_ == nullptr) {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    this->mark_failed();
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &ElitePartnerMqtt::mqtt_event_handler_,
                                 this);
  esp_err_t err = esp_mqtt_client_start(client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_mqtt_client_start failed (%d)", err);
    this->mark_failed();
  }
}

void ElitePartnerMqtt::mqtt_event_handler_(void *arg, esp_event_base_t base, int32_t id,
                                           void *data) {
  auto *self = static_cast<ElitePartnerMqtt *>(arg);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected to partner broker %s:%u", self->broker_.c_str(), self->port_);
      self->connected_ = true;
      self->heartbeat_();  // initial dump on connect
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected from partner broker");
      self->connected_ = false;
      break;
    case MQTT_EVENT_ERROR:
      ESP_LOGW(TAG, "Partner MQTT error event");
      break;
    default:
      break;
  }
}

void ElitePartnerMqtt::hook_sensor_callbacks_() {
#ifdef USE_SENSOR
  for (auto *s : App.get_sensors()) {
    s->add_on_state_callback([this, s](float state) {
      if (std::isnan(state))
        return;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.3f", state);
      this->publish("sensor/" + slug(s->get_object_id()) + "/state", buf, 0, false);
    });
  }
#endif
#ifdef USE_BINARY_SENSOR
  for (auto *s : App.get_binary_sensors()) {
    s->add_on_state_callback([this, s](bool state) {
      this->publish("binary_sensor/" + slug(s->get_object_id()) + "/state",
                    state ? "ON" : "OFF", 0, false);
    });
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (auto *s : App.get_text_sensors()) {
    s->add_on_state_callback([this, s](const std::string &state) {
      this->publish("text_sensor/" + slug(s->get_object_id()) + "/state", state, 0, false);
    });
  }
#endif
}

void ElitePartnerMqtt::heartbeat_() {
  // Republish current snapshot of every sensor so a fresh subscriber
  // gets immediate state without waiting for the next change.
#ifdef USE_SENSOR
  for (auto *s : App.get_sensors()) {
    if (std::isnan(s->state))
      continue;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", s->state);
    this->publish("sensor/" + slug(s->get_object_id()) + "/state", buf, 0, true);
  }
#endif
#ifdef USE_BINARY_SENSOR
  for (auto *s : App.get_binary_sensors()) {
    if (!s->has_state())
      continue;
    this->publish("binary_sensor/" + slug(s->get_object_id()) + "/state",
                  s->state ? "ON" : "OFF", 0, true);
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (auto *s : App.get_text_sensors()) {
    if (!s->has_state())
      continue;
    this->publish("text_sensor/" + slug(s->get_object_id()) + "/state", s->state, 0, true);
  }
#endif
}

bool ElitePartnerMqtt::publish(const std::string &suffix, const std::string &payload, int qos,
                               bool retain) {
  if (!connected_ || client_ == nullptr)
    return false;
  std::string topic = topic_prefix_ + "/" + suffix;
  int msg_id = esp_mqtt_client_publish(client_, topic.c_str(), payload.data(),
                                       static_cast<int>(payload.size()), qos, retain ? 1 : 0);
  return msg_id != -1;
}

}  // namespace elite_partner_mqtt
}  // namespace esphome

#endif  // USE_ESP_IDF
