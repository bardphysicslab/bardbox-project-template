#pragma once

#include "BardBoxStorage.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)

namespace bbcore {

// LittleFS on the "spiffs"-labelled data partition of the BardBox 8 MiB layout.
// Mounting never formats: a failed mount is reported as unavailable storage.
// Physical durability (power loss, wear) is not verified.
class LittleFsStorage : public StorageAdapter {
 public:
  const char *name() const override { return "littlefs"; }
  bool mount() override;
  bool makeDir(const char *path) override;
  bool writeFile(const char *path, const uint8_t *data, size_t len) override;
  bool readFile(const char *path, uint8_t *buf, size_t cap, size_t &len) override;
  bool rename(const char *from, const char *to) override;
  bool remove(const char *path) override;
  bool exists(const char *path) override;
  bool list(const char *dir, ListCallback callback, void *context) override;
};

}  // namespace bbcore

#endif
