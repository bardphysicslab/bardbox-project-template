#include "BardBoxLittleFsStorage.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)

#include <LittleFS.h>

namespace bbcore {

bool LittleFsStorage::mount() {
  return LittleFS.begin(false /* never format */, "/littlefs", 4, "spiffs");
}

bool LittleFsStorage::makeDir(const char *path) { return LittleFS.mkdir(path); }

bool LittleFsStorage::writeFile(const char *path, const uint8_t *data, size_t len) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  const size_t written = f.write(data, len);
  f.close();  // LittleFS commits on close
  return written == len;
}

bool LittleFsStorage::readFile(const char *path, uint8_t *buf, size_t cap, size_t &len) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const size_t size = f.size();
  if (size > cap) {
    f.close();
    return false;
  }
  len = f.read(buf, size);
  f.close();
  return len == size;
}

bool LittleFsStorage::rename(const char *from, const char *to) { return LittleFS.rename(from, to); }

bool LittleFsStorage::remove(const char *path) { return LittleFS.remove(path); }

bool LittleFsStorage::exists(const char *path) { return LittleFS.exists(path); }

bool LittleFsStorage::list(const char *dir, ListCallback callback, void *context) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return false;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const char *full = f.name();
    const char *slash = strrchr(full, '/');
    callback(slash ? slash + 1 : full, context);
    f.close();
  }
  d.close();
  return true;
}

}  // namespace bbcore

#endif
