#ifdef USE_ESP_IDF

#include "elite_solarman_v5.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace elite_solarman_v5 {

static const char *const TAG = "elite_solarman_v5";

// V5 protocol constants. Source: pysolarmanv5 docs.
//   https://pysolarmanv5.readthedocs.io/en/stable/solarmanv5_protocol.html
namespace v5 {
constexpr uint8_t START_BYTE = 0xA5;
constexpr uint8_t END_BYTE   = 0x15;

// Control codes used by the stock LSW3 stick when pushing logger->cloud.
constexpr uint16_t CTRL_HANDSHAKE_REQ = 0x4110;
constexpr uint16_t CTRL_HANDSHAKE_RESP = 0x1110;
constexpr uint16_t CTRL_DATA_REQ      = 0x4210;
constexpr uint16_t CTRL_DATA_RESP     = 0x1210;
constexpr uint16_t CTRL_INFO_REQ      = 0x4310;
constexpr uint16_t CTRL_HEARTBEAT_REQ = 0x4710;
constexpr uint16_t CTRL_HEARTBEAT_RESP = 0x1710;
}  // namespace v5

void EliteSolarmanV5::set_logger_serial(const std::string &decimal) {
  // Solarman serials are printed as 10-digit decimal on the LSW3
  // sticker. They fit in a uint32_t and are sent little-endian.
  uint32_t v = 0;
  for (char c : decimal) {
    if (c < '0' || c > '9')
      continue;
    v = v * 10 + static_cast<uint32_t>(c - '0');
  }
  logger_serial_le_ = v;
}

void EliteSolarmanV5::setup() {
  boot_time_s_ = millis() / 1000;
  if (logger_serial_le_ == 0) {
    ESP_LOGW(TAG,
             "Solarman logger_serial is 0 — Solarman cloud will silently "
             "reject every frame. Set the real serial from the LSW3 sticker "
             "in secrets.yaml.");
  }
  if (register_blocks_.empty()) {
    ESP_LOGW(TAG,
             "No register_blocks configured — Solarman cloud will only see "
             "heartbeats. Add register_blocks to the YAML to mirror Modbus "
             "snapshots.");
  } else {
    ESP_LOGCONFIG(TAG, "Mirroring %zu register block(s) to Solarman cloud",
                  register_blocks_.size());
  }
  start_connect_();
}

void EliteSolarmanV5::loop() {
  const uint32_t now = millis();

  if (sock_.load(std::memory_order_acquire) < 0) {
    // Reconnect throttled to once every 30s. Per-instance state so
    // multiple EliteSolarmanV5 components don't trample each other's
    // back-off windows.
    if (now - last_reconnect_attempt_ > 30000) {
      last_reconnect_attempt_ = now;
      start_connect_();
    }
    return;
  }

  if (now - last_heartbeat_ >= 60000) {
    last_heartbeat_ = now;
    send_heartbeat_();
  }

  if (now - last_push_ >= push_interval_ms_) {
    last_push_ = now;
    // Order matters: send the snapshot we already have, *then* queue
    // fresh reads for the next cycle. This keeps the V5 push interval
    // independent of Modbus latency — a slow inverter doesn't push our
    // frame timing around.
    send_data_report_();
    issue_register_reads_();
  }
}

void EliteSolarmanV5::dump_config() {
  ESP_LOGCONFIG(TAG, "Elite Solarman V5:");
  ESP_LOGCONFIG(TAG, "  Server:           %s:%u", server_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Logger serial:    %" PRIu32 " (LE)", logger_serial_le_);
  ESP_LOGCONFIG(TAG, "  Push interval:    every %" PRIu32 " ms", push_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Modbus parent:    %p", static_cast<void *>(modbus_));
  ESP_LOGCONFIG(TAG, "  Modbus address:   0x%02X", modbus_address_);
  ESP_LOGCONFIG(TAG, "  Register blocks:  %zu", register_blocks_.size());
  for (const auto &b : register_blocks_) {
    ESP_LOGCONFIG(TAG, "    - 0x%04X (%u registers)", b.start_address, b.register_count);
  }
}

void EliteSolarmanV5::start_connect_() {
  if (server_.empty()) {
    ESP_LOGE(TAG, "No server configured");
    return;
  }
  // Don't pile up a second connect task if one is still running.
  bool expected = false;
  if (!connecting_.compare_exchange_strong(expected, true)) {
    ESP_LOGD(TAG, "connect already in flight, skipping");
    return;
  }
  // 4 KiB stack is comfortably enough for getaddrinfo + a single
  // connect; tskIDLE_PRIORITY+1 keeps it well below the ESPHome
  // main loop priority so polling never starves.
  TaskHandle_t handle = nullptr;
  BaseType_t ok = xTaskCreate(
      &EliteSolarmanV5::connect_task_,
      "sol_v5_conn",
      4096,
      this,
      tskIDLE_PRIORITY + 1,
      &handle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "xTaskCreate failed for connect task");
    connecting_.store(false, std::memory_order_release);
  }
}

void EliteSolarmanV5::connect_task_(void *arg) {
  auto *self = static_cast<EliteSolarmanV5 *>(arg);

  struct addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = nullptr;
  char port_str[8];
  std::snprintf(port_str, sizeof(port_str), "%u", self->port_);
  int err = getaddrinfo(self->server_.c_str(), port_str, &hints, &res);
  if (err != 0 || res == nullptr) {
    ESP_LOGW(TAG, "DNS lookup failed for %s: %d", self->server_.c_str(), err);
    self->connecting_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }

  // Use lwIP's POSIX-shim functions explicitly. Inside namespace
  // esphome::elite_solarman_v5, the unqualified name `socket` would
  // bind to the sibling namespace `esphome::socket` rather than
  // lwIP's macro `lwip_socket(...)`. Same for `connect`, `close`,
  // `setsockopt`, `send`, `recv` further below.
  int s = ::lwip_socket(res->ai_family, res->ai_socktype, 0);
  if (s < 0) {
    ESP_LOGW(TAG, "socket() failed: %d", errno);
    freeaddrinfo(res);
    self->connecting_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }

  // Reasonable connect timeout (still applies to send/recv afterwards).
  struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
  ::lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  ::lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (::lwip_connect(s, res->ai_addr, res->ai_addrlen) != 0) {
    ESP_LOGW(TAG, "connect() to %s:%u failed: %d", self->server_.c_str(), self->port_, errno);
    ::lwip_close(s);
    freeaddrinfo(res);
    self->connecting_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }
  freeaddrinfo(res);

  // Publish fd to the main loop. acquire/release ordering pairs with
  // the load in loop().
  self->sock_.store(s, std::memory_order_release);
  self->connecting_.store(false, std::memory_order_release);

  ESP_LOGI(TAG, "Connected to %s:%u (V5 logger sn=%" PRIu32 ")", self->server_.c_str(),
           self->port_, self->logger_serial_le_);

  // First frame after connect is a logger info / handshake. We send a
  // minimal payload so the cloud associates the TCP session with our
  // logger SN before any data frame arrives. Safe to call from this
  // task because send_() reads sock_ atomically and writes are not
  // racing the main loop yet (modbus snapshot push waits on next
  // loop tick).
  std::vector<uint8_t> empty;
  self->send_(self->build_frame(v5::CTRL_HANDSHAKE_REQ, empty));

  vTaskDelete(nullptr);
}

void EliteSolarmanV5::disconnect_() {
  int s = sock_.exchange(-1, std::memory_order_acq_rel);
  if (s >= 0) {
    ::lwip_close(s);
  }
}

bool EliteSolarmanV5::send_(const std::vector<uint8_t> &frame) {
  int s = sock_.load(std::memory_order_acquire);
  if (s < 0)
    return false;
  ssize_t n = ::lwip_send(s, frame.data(), frame.size(), 0);
  if (n < 0 || static_cast<size_t>(n) != frame.size()) {
    ESP_LOGW(TAG, "send() failed (%zd of %zu): errno=%d", n, frame.size(), errno);
    disconnect_();
    return false;
  }
  return true;
}

void EliteSolarmanV5::send_heartbeat_() {
  std::vector<uint8_t> empty;
  send_(build_frame(v5::CTRL_HEARTBEAT_REQ, empty));
}

void EliteSolarmanV5::send_data_report_() {
  // Stock LSW3 sends one V5 frame per Modbus block. We mirror that:
  // each cached block becomes its own data-report frame so the cloud
  // can dispatch them register-range by register-range.
  if (register_blocks_.empty())
    return;
  size_t sent = 0;
  for (const auto &block : register_blocks_) {
    auto it = register_cache_.find(block.start_address);
    if (it == register_cache_.end() || it->second.empty()) {
      // No cached data for this block yet — reads are still in flight
      // (first cycle after boot) or the inverter didn't respond.
      continue;
    }
    const size_t expected_bytes = static_cast<size_t>(block.register_count) * 2;
    if (it->second.size() != expected_bytes) {
      ESP_LOGW(TAG,
               "Block 0x%04X cache size %zu != expected %zu — skipping",
               block.start_address, it->second.size(), expected_bytes);
      continue;
    }
    auto modbus_resp = build_modbus_read_response(block.register_count, it->second);
    if (!send_(build_frame(v5::CTRL_DATA_REQ, modbus_resp))) {
      // TCP write failed — disconnect_() already invoked, abort the rest
      // of this push cycle. Reconnect kicks in on the next loop tick.
      return;
    }
    ++sent;
  }
  ESP_LOGD(TAG, "Pushed %zu/%zu register block(s) to Solarman cloud",
           sent, register_blocks_.size());
}

void EliteSolarmanV5::issue_register_reads_() {
  if (modbus_ == nullptr) {
    ESP_LOGW(TAG, "No modbus_controller bound — cannot issue reads");
    return;
  }
  for (const auto &block : register_blocks_) {
    const uint16_t start = block.start_address;
    const uint16_t count = block.register_count;
    auto cmd = modbus_controller::ModbusCommandItem::create_read_command(
        modbus_, modbus_controller::ModbusRegisterType::HOLDING, start, count,
        [this, start, count](modbus_controller::ModbusRegisterType /*type*/,
                             uint16_t /*addr*/,
                             const std::vector<uint8_t> &data) {
          const size_t expected = static_cast<size_t>(count) * 2;
          if (data.size() != expected) {
            ESP_LOGW(TAG,
                     "Block 0x%04X: got %zu bytes, expected %zu — discarding",
                     start, data.size(), expected);
            return;
          }
          this->register_cache_[start] = data;
        });
    modbus_->queue_command(cmd);
  }
}

std::vector<uint8_t> EliteSolarmanV5::build_modbus_read_response(
    uint16_t register_count, const std::vector<uint8_t> &reg_data) const {
  // Modbus RTU function-0x03 response layout:
  //   [slave_addr][0x03][byte_count][data_bytes...][crc_lo][crc_hi]
  // CRC covers everything from slave_addr through the last data byte.
  std::vector<uint8_t> rsp;
  rsp.reserve(3 + reg_data.size() + 2);
  rsp.push_back(modbus_address_);
  rsp.push_back(0x03);  // READ_HOLDING_REGISTERS
  rsp.push_back(static_cast<uint8_t>(register_count * 2));
  rsp.insert(rsp.end(), reg_data.begin(), reg_data.end());
  const uint16_t crc = modbus_crc16(rsp.data(), rsp.size());
  rsp.push_back(static_cast<uint8_t>(crc & 0xFF));         // low byte first
  rsp.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));  // then high byte
  return rsp;
}

uint16_t EliteSolarmanV5::modbus_crc16(const uint8_t *data, size_t len) {
  // Standard Modbus CRC-16: polynomial 0xA001 (reflected 0x8005),
  // initial value 0xFFFF, no final XOR, low byte transmitted first.
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

std::vector<uint8_t> EliteSolarmanV5::build_frame(uint16_t control_code,
                                                  const std::vector<uint8_t> &modbus_payload) {
  // Frame layout (all multi-byte fields little-endian):
  //   0xA5 | length(2) | ctrl(2) | seq(2) | logger(4) | frame_type(1)
  //        | sensor_type(2) | total_uptime(4) | power_on(4) | offset_time(4)
  //        | modbus_payload | checksum(1) | 0x15
  //
  // `length` is the count of bytes between (and not including) the
  // length field itself and the checksum byte — i.e. ctrl..modbus.

  std::vector<uint8_t> meta;
  meta.reserve(15 + modbus_payload.size());

  // ctrl
  meta.push_back(static_cast<uint8_t>(control_code & 0xFF));
  meta.push_back(static_cast<uint8_t>((control_code >> 8) & 0xFF));
  // seq
  uint16_t seq = ++seq_;
  meta.push_back(static_cast<uint8_t>(seq & 0xFF));
  meta.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
  // logger sn (LE)
  meta.push_back(static_cast<uint8_t>(logger_serial_le_ & 0xFF));
  meta.push_back(static_cast<uint8_t>((logger_serial_le_ >> 8) & 0xFF));
  meta.push_back(static_cast<uint8_t>((logger_serial_le_ >> 16) & 0xFF));
  meta.push_back(static_cast<uint8_t>((logger_serial_le_ >> 24) & 0xFF));

  // frame_type: 0x02 == "data" for stock LSW3
  meta.push_back(0x02);
  // sensor_type: 0x0000 (default)
  meta.push_back(0x00);
  meta.push_back(0x00);
  // total uptime since logger ever booted (s)
  uint32_t uptime = (millis() / 1000);
  meta.push_back(static_cast<uint8_t>(uptime & 0xFF));
  meta.push_back(static_cast<uint8_t>((uptime >> 8) & 0xFF));
  meta.push_back(static_cast<uint8_t>((uptime >> 16) & 0xFF));
  meta.push_back(static_cast<uint8_t>((uptime >> 24) & 0xFF));
  // power on time = uptime since current boot (s)
  uint32_t pwr_on = uptime - boot_time_s_;
  meta.push_back(static_cast<uint8_t>(pwr_on & 0xFF));
  meta.push_back(static_cast<uint8_t>((pwr_on >> 8) & 0xFF));
  meta.push_back(static_cast<uint8_t>((pwr_on >> 16) & 0xFF));
  meta.push_back(static_cast<uint8_t>((pwr_on >> 24) & 0xFF));
  // offset time (epoch seconds since 2000-01-01 — 0 if no RTC)
  meta.push_back(0x00);
  meta.push_back(0x00);
  meta.push_back(0x00);
  meta.push_back(0x00);

  // append modbus payload
  meta.insert(meta.end(), modbus_payload.begin(), modbus_payload.end());

  // header
  std::vector<uint8_t> out;
  out.reserve(meta.size() + 5);
  out.push_back(v5::START_BYTE);
  uint16_t len = static_cast<uint16_t>(meta.size() - 4);  // ctrl..end of payload, excluding ctrl/seq fields by spec
  // pysolarmanv5 documents `length` as bytes from frame_type to end of payload.
  // To keep parity with reference implementations, recompute below.
  // length = frame_type(1) + sensor_type(2) + uptime(4) + pwr(4) + off(4) + payload
  len = static_cast<uint16_t>(1 + 2 + 4 + 4 + 4 + modbus_payload.size());
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  out.insert(out.end(), meta.begin(), meta.end());

  // checksum: sum of all bytes from `length` (out[1]) through last
  // payload byte (out[end]), modulo 256. Stock LSW3 implementations
  // sum starting at out[1] inclusive.
  uint8_t cksum = 0;
  for (size_t i = 1; i < out.size(); i++)
    cksum += out[i];
  out.push_back(cksum);
  out.push_back(v5::END_BYTE);
  return out;
}

}  // namespace elite_solarman_v5
}  // namespace esphome

#endif  // USE_ESP_IDF
