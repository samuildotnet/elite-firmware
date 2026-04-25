#pragma once

#ifdef USE_ESP_IDF

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include "mqtt_client.h"

namespace esphome {
namespace elite_partner_mqtt {

/**
 * Secondary MQTT-TLS publisher.
 *
 * ESPHome's stock `mqtt:` component connects to a single broker. The Elite
 * Energy dongle needs to publish telemetry to a *partner* broker in
 * parallel (anonymous TLS, different broker). This component owns a
 * second `esp_mqtt_client_handle_t` that runs alongside the primary
 * client.
 *
 * Lifecycle:
 *   - setup():   create esp-mqtt client, register sensor callbacks
 *   - loop():    no-op (esp-mqtt has its own task)
 *   - on sensor state change: publish JSON `{topic_prefix}/sensor/{name}/state`
 *   - on periodic timer: republish all sensors (dead-man heartbeat)
 *
 * Topic taxonomy mirrors the stock `mqtt:` block, so a partner subscriber
 * sees the same payload structure on a different broker.
 */
class ElitePartnerMqtt : public Component {
 public:
  void set_broker(const std::string &broker) { broker_ = broker; }
  void set_port(uint16_t port) { port_ = port; }
  void set_username(const std::string &u) { username_ = u; }
  void set_password(const std::string &p) { password_ = p; }
  void set_client_id(const std::string &c) { client_id_ = c; }
  void set_topic_prefix(const std::string &t) { topic_prefix_ = t; }
  void set_publish_interval(uint32_t ms) { publish_interval_ms_ = ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  /// Publish a single value. Topic is `{topic_prefix}/{suffix}`. Payload
  /// is sent as-is (UTF-8). Returns true if the client accepted the
  /// publish (queued); false if not connected.
  bool publish(const std::string &suffix, const std::string &payload, int qos = 0,
               bool retain = false);

 protected:
  /// Build the esp-mqtt config struct from member settings and start
  /// the client. Idempotent — safe to call on reconnect.
  void start_client_();

  /// Walk App.sensors / App.binary_sensors / App.text_sensors and
  /// register state-change callbacks that publish to this broker.
  void hook_sensor_callbacks_();

  /// Forced re-publish of all sensors (keep-alive heartbeat).
  void heartbeat_();

  static void mqtt_event_handler_(void *arg, esp_event_base_t base, int32_t id,
                                  void *data);

  std::string broker_;
  uint16_t port_{8883};
  std::string username_;
  std::string password_;
  std::string client_id_;
  std::string topic_prefix_;
  uint32_t publish_interval_ms_{30000};

  esp_mqtt_client_handle_t client_{nullptr};
  bool connected_{false};
  uint32_t last_heartbeat_{0};
};

}  // namespace elite_partner_mqtt
}  // namespace esphome

#endif  // USE_ESP_IDF
