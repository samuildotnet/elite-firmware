// Host-test shim for ESPHome's component.h. The production header
// pulls in the full ESPHome runtime (which is built around ESP-IDF /
// Arduino), so on a developer machine we substitute the minimum
// surface that our parsing layer references.
#pragma once

namespace esphome {

namespace setup_priority {
// Only the relative ordering matters here; mirrors the upstream value.
constexpr float BUS = 1000.0f;
constexpr float HARDWARE = 900.0f;
}  // namespace setup_priority

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }
};

}  // namespace esphome
