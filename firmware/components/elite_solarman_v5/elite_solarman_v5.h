#pragma once

#ifdef USE_ESP_IDF

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/modbus_controller/modbus_controller.h"

namespace esphome {
namespace elite_solarman_v5 {

/**
 * Solarman V5 binary protocol publisher.
 *
 * Connects via plain TCP (no TLS — Solarman cloud doesn't require it)
 * to a V5 ingestion server and pushes:
 *   - heartbeat frames every 60s (control code 0x4710)
 *   - data-report frames every `push_interval` (control code 0x4210)
 *     containing the latest Modbus register window read by ESPHome's
 *     `modbus_controller`.
 *
 * Logger serial: 4-byte little-endian integer derived from the 10-digit
 * decimal sticker on the original LSW3 dongle. Without a *real* serial
 * the Solarman cloud will silently drop every frame — this component
 * still runs (and logs) so that the dongle stays operational, but no
 * data shows up in the customer's Solarman / Deye-app account.
 */
class EliteSolarmanV5 : public Component {
 public:
  void set_server(const std::string &s) { server_ = s; }
  void set_port(uint16_t p) { port_ = p; }
  void set_logger_serial(const std::string &decimal);
  void set_push_interval(uint32_t ms) { push_interval_ms_ = ms; }
  void set_modbus_controller(modbus_controller::ModbusController *c) { modbus_ = c; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  /// Encode an arbitrary Modbus RTU payload inside a V5 frame and
  /// queue it for transmission. Public so unit tests can poke at it.
  std::vector<uint8_t> build_frame(uint16_t control_code, const std::vector<uint8_t> &modbus_payload);

 protected:
  void connect_();
  void disconnect_();
  bool send_(const std::vector<uint8_t> &frame);
  void send_heartbeat_();
  void send_data_report_();

  /// Capture the last Modbus reply seen by the modbus_controller and
  /// fold it into a V5 data-report frame.
  std::vector<uint8_t> snapshot_modbus_payload_();

  std::string server_;
  uint16_t port_{10000};
  uint32_t logger_serial_le_{0};   // 4-byte little-endian
  uint32_t push_interval_ms_{60000};
  modbus_controller::ModbusController *modbus_{nullptr};

  int sock_{-1};
  uint16_t seq_{0};
  uint32_t last_push_{0};
  uint32_t last_heartbeat_{0};
  uint32_t boot_time_s_{0};
};

}  // namespace elite_solarman_v5
}  // namespace esphome

#endif  // USE_ESP_IDF
