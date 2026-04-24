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
  connect_();
}

void EliteSolarmanV5::loop() {
  const uint32_t now = millis();

  if (sock_ < 0) {
    // Reconnect throttled to once every 30s.
    static uint32_t last_attempt = 0;
    if (now - last_attempt > 30000) {
      last_attempt = now;
      connect_();
    }
    return;
  }

  if (now - last_heartbeat_ >= 60000) {
    last_heartbeat_ = now;
    send_heartbeat_();
  }

  if (now - last_push_ >= push_interval_ms_) {
    last_push_ = now;
    send_data_report_();
  }
}

void EliteSolarmanV5::dump_config() {
  ESP_LOGCONFIG(TAG, "Elite Solarman V5:");
  ESP_LOGCONFIG(TAG, "  Server:           %s:%u", server_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Logger serial:    %" PRIu32 " (LE)", logger_serial_le_);
  ESP_LOGCONFIG(TAG, "  Push interval:    every %" PRIu32 " ms", push_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Modbus parent:    %p", static_cast<void *>(modbus_));
}

void EliteSolarmanV5::connect_() {
  if (server_.empty()) {
    ESP_LOGE(TAG, "No server configured");
    return;
  }

  struct addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = nullptr;
  char port_str[8];
  std::snprintf(port_str, sizeof(port_str), "%u", port_);
  int err = getaddrinfo(server_.c_str(), port_str, &hints, &res);
  if (err != 0 || res == nullptr) {
    ESP_LOGW(TAG, "DNS lookup failed for %s: %d", server_.c_str(), err);
    return;
  }

  sock_ = socket(res->ai_family, res->ai_socktype, 0);
  if (sock_ < 0) {
    ESP_LOGW(TAG, "socket() failed: %d", errno);
    freeaddrinfo(res);
    return;
  }

  // Reasonable connect timeout
  struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
  setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (connect(sock_, res->ai_addr, res->ai_addrlen) != 0) {
    ESP_LOGW(TAG, "connect() to %s:%u failed: %d", server_.c_str(), port_, errno);
    close(sock_);
    sock_ = -1;
    freeaddrinfo(res);
    return;
  }
  freeaddrinfo(res);

  ESP_LOGI(TAG, "Connected to %s:%u (V5 logger sn=%" PRIu32 ")", server_.c_str(), port_,
           logger_serial_le_);

  // First frame after connect is a logger info / handshake. We send a
  // minimal payload so the cloud associates the TCP session with our
  // logger SN before any data frame arrives.
  std::vector<uint8_t> empty;
  send_(build_frame(v5::CTRL_HANDSHAKE_REQ, empty));
}

void EliteSolarmanV5::disconnect_() {
  if (sock_ >= 0) {
    close(sock_);
    sock_ = -1;
  }
}

bool EliteSolarmanV5::send_(const std::vector<uint8_t> &frame) {
  if (sock_ < 0)
    return false;
  ssize_t n = ::send(sock_, frame.data(), frame.size(), 0);
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
  auto modbus_payload = snapshot_modbus_payload_();
  if (modbus_payload.empty())
    return;
  send_(build_frame(v5::CTRL_DATA_REQ, modbus_payload));
}

std::vector<uint8_t> EliteSolarmanV5::snapshot_modbus_payload_() {
  // TODO: hook into modbus_controller's last-response cache. ESPHome's
  // modbus_controller doesn't expose the raw last-response buffer
  // publicly — implementing this properly requires either:
  //   (a) intercepting modbus::ModbusDevice::on_modbus_data() via a
  //       custom subclass, or
  //   (b) re-issuing the same Modbus reads with a parallel client.
  //
  // For now we return an empty payload and only heartbeats keep the TCP
  // session alive. Tracked in elite-firmware#1.
  return {};
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
