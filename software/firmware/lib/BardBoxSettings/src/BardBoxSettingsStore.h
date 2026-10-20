#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bbcore {

// Persistent home of the saved device settings record. One record, replaced
// as a whole: save() must leave either the old or the new complete record
// (ESP32: one NVS blob). Injected so hosts can test failures.
class SettingsStore {
 public:
  enum class Load : uint8_t { None, Ok, Failed };
  virtual ~SettingsStore() {}
  virtual const char *name() const = 0;
  virtual Load load(uint8_t *buf, size_t cap, size_t &len) = 0;
  virtual bool save(const uint8_t *data, size_t len) = 0;
};

}  // namespace bbcore
