#pragma once

#include "BardBoxSettingsStore.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace bbcore {

// ESP32: one NVS blob ("bb-settings" / "s1"). NVS replaces a key as a whole,
// so a failed or interrupted save leaves the previous record. The NVS
// partition is outside every firmware write range, so an ordinary reflash
// keeps the saved settings; an erase of the chip removes them.
class NvsSettingsStore : public SettingsStore {
 public:
  const char *name() const override { return "nvs"; }
  Load load(uint8_t *buf, size_t cap, size_t &len) override;
  bool save(const uint8_t *data, size_t len) override;
};

}  // namespace bbcore
#endif
