#pragma once

#ifdef USE_ESP_IDF

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/modbus_controller/modbus_controller.h"

namespace esphome {
namespace elite_solarman_v5 {

/// One contiguous block of holding registers we mirror to Solarman cloud.
/// Each block is captured by issuing a parallel READ_HOLDING_REGISTERS
/// command on top of whatever ESPHome's modbus_controller is already
/// polling. The captured raw bytes go straight into the Modbus payload
/// of a V5 data-report frame.
struct RegisterBlock {
  uint16_t start_address;
  uint16_t register_count;
};

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
 *
 * Modbus payload capture: we issue parallel `READ_HOLDING_REGISTERS`
 * commands against the same `modbus_controller` that ESPHome is using
 * for its sensor polling, then rebuild a Modbus RTU response frame
 * (slave + 0x03 + byte_count + data + CRC16) and embed that as the V5
 * frame's Modbus payload. Solarman cloud parses this exactly the way
 * it would parse a stock LSW3's response. The doubling of Modbus
 * traffic is negligible at 9600 baud — even a full 12-block sweep
 * every 60s uses < 5 % of bus capacity.
 */
class EliteSolarmanV5 : public Component {
 public:
  void set_server(const std::string &s) { server_ = s; }
  void set_port(uint16_t p) { port_ = p; }
  void set_logger_serial(const std::string &decimal);
  void set_push_interval(uint32_t ms) { push_interval_ms_ = ms; }
  void set_modbus_controller(modbus_controller::ModbusController *c) { modbus_ = c; }
  void set_modbus_address(uint8_t a) { modbus_address_ = a; }
  void add_register_block(uint16_t start, uint16_t count) {
    register_blocks_.push_back({start, count});
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  /// Encode an arbitrary Modbus RTU payload inside a V5 frame and
  /// queue it for transmission. Public so unit tests can poke at it.
  std::vector<uint8_t> build_frame(uint16_t control_code, const std::vector<uint8_t> &modbus_payload);

  /// Reconstruct a Modbus RTU function-0x03 (read holding registers)
  /// response: `[addr][0x03][byte_count][reg_data...][crc_lo][crc_hi]`.
  /// `reg_data` is the raw byte buffer ESPHome handed back from
  /// `ModbusCommandItem::on_data_func`. Public for unit testing.
  std::vector<uint8_t> build_modbus_read_response(uint16_t register_count,
                                                  const std::vector<uint8_t> &reg_data) const;

  /// Modbus CRC-16 (polynomial 0xA001, initial 0xFFFF). Standard for
  /// Modbus RTU. Public for unit testing.
  static uint16_t modbus_crc16(const uint8_t *data, size_t len);

 protected:
  /// Spawn a one-shot FreeRTOS task that performs blocking DNS + TCP
  /// connect off the ESPHome main loop. The task atomically publishes
  /// the resulting fd into sock_ and resets connecting_ when done, so
  /// loop() never stalls waiting for the network.
  void start_connect_();

  /// Static FreeRTOS entry point. `arg` is `this`.
  static void connect_task_(void *arg);

  void disconnect_();
  bool send_(const std::vector<uint8_t> &frame);
  void send_heartbeat_();
  void send_data_report_();

  /// Queue READ_HOLDING_REGISTERS commands for every configured
  /// register block. Their responses populate `register_cache_`
  /// asynchronously (typically within a few hundred ms at 9600 baud).
  void issue_register_reads_();

  std::string server_;
  uint16_t port_{10000};
  uint32_t logger_serial_le_{0};   // 4-byte little-endian
  uint32_t push_interval_ms_{60000};
  modbus_controller::ModbusController *modbus_{nullptr};
  uint8_t modbus_address_{0x01};

  std::vector<RegisterBlock> register_blocks_;
  /// Latest raw register data per block, keyed by start_address.
  /// Populated asynchronously by the modbus_controller when our
  /// queued read commands complete. Read on the main loop when we
  /// build the next data-report frame.
  std::map<uint16_t, std::vector<uint8_t>> register_cache_;

  // sock_ is read by loop() and written by the connect task — atomic
  // because we touch it from two FreeRTOS tasks. -1 means "not connected".
  std::atomic<int> sock_{-1};

  // True between start_connect_() and the connect task completing.
  // Prevents respawning a second connect task on top of an in-flight one.
  std::atomic<bool> connecting_{false};

  uint16_t seq_{0};
  uint32_t last_push_{0};
  uint32_t last_heartbeat_{0};
  uint32_t last_reconnect_attempt_{0};
  uint32_t boot_time_s_{0};
};

}  // namespace elite_solarman_v5
}  // namespace esphome

#endif  // USE_ESP_IDF
