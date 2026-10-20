#pragma once
// In-memory StorageAdapter for host tests, with fault injection. A "restart"
// is a new BardBoxBuffer begun on the same FakeStorage object.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "BardBoxStorage.h"

class FakeStorage : public bbcore::StorageAdapter {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> dirs;
  bool mountFails = false;
  int failWrites = 0;   // fail the next N writes (nothing written)
  int failRenames = 0;  // fail the next N renames (nothing renamed)
  int failRemoves = 0;  // fail the next N removes (file stays)
  int failWriteAt = 0;   // fail only the Nth write from now (1 = next)
  std::string failRenameOf;  // always fail renaming this path
  int mounts = 0;
  bool formatted = false;  // must never become true

  const char *name() const override { return "host"; }
  bool mount() override {
    ++mounts;
    return !mountFails;
  }
  bool makeDir(const char *path) override {
    dirs.insert(path);
    return true;
  }
  bool writeFile(const char *path, const uint8_t *data, size_t len) override {
    if (failWrites > 0) {
      --failWrites;
      return false;
    }
    if (failWriteAt > 0 && --failWriteAt == 0) return false;
    files[path].assign(data, data + len);
    return true;
  }
  bool readFile(const char *path, uint8_t *buf, size_t cap, size_t &len) override {
    auto it = files.find(path);
    if (it == files.end() || it->second.size() > cap) return false;
    len = it->second.size();
    std::copy(it->second.begin(), it->second.end(), buf);
    return true;
  }
  bool rename(const char *from, const char *to) override {
    if (failRenames > 0) {
      --failRenames;
      return false;
    }
    if (!failRenameOf.empty() && failRenameOf == from) return false;
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  bool remove(const char *path) override {
    if (failRemoves > 0) {
      --failRemoves;
      return false;
    }
    return files.erase(path) > 0;
  }
  bool exists(const char *path) override { return files.count(path) || dirs.count(path); }
  bool list(const char *dir, ListCallback callback, void *context) override {
    if (!dirs.count(dir)) return false;
    const std::string prefix = std::string(dir) + "/";
    for (const auto &f : files)
      if (f.first.compare(0, prefix.size(), prefix) == 0 &&
          f.first.find('/', prefix.size()) == std::string::npos)
        callback(f.first.c_str() + prefix.size(), context);
    return true;
  }
};
