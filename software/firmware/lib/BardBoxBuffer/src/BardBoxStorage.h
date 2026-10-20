#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bbcore {

// Minimal file storage used by BardBoxBuffer. Implementations must never
// format or erase on their own: a failed mount is reported, not repaired.
class StorageAdapter {
 public:
  using ListCallback = void (*)(const char *name, void *context);

  virtual ~StorageAdapter() {}
  virtual const char *name() const = 0;
  virtual bool mount() = 0;
  virtual bool makeDir(const char *path) = 0;
  // Writes a whole file (replacing any existing one) and closes it.
  virtual bool writeFile(const char *path, const uint8_t *data, size_t len) = 0;
  virtual bool readFile(const char *path, uint8_t *buf, size_t cap, size_t &len) = 0;
  // Replaces `to` if it exists.
  virtual bool rename(const char *from, const char *to) = 0;
  virtual bool remove(const char *path) = 0;
  virtual bool exists(const char *path) = 0;
  // Calls back with each entry name (no directory part) in `dir`.
  virtual bool list(const char *dir, ListCallback callback, void *context) = 0;
};

}  // namespace bbcore
