#pragma once
// In-memory settings store with fault injection, for host tests only.
#include <stdint.h>
#include <string.h>

#include <string>

#include "BardBoxSettingsStore.h"

class FakeSettingsStore : public bbcore::SettingsStore {
 public:
  std::string bytes;      // the one saved record ("" = nothing saved)
  int failSaves = 0;      // next N saves fail and keep the old record
  bool failLoads = false; // loads report a read failure
  bool saveLiesFailed = false;  // the save is stored but reported as failed
  int saves = 0;

  const char *name() const override { return "fake"; }
  Load load(uint8_t *buf, size_t cap, size_t &len) override {
    if (failLoads) return Load::Failed;
    if (bytes.empty()) return Load::None;
    if (bytes.size() > cap) return Load::Failed;
    memcpy(buf, bytes.data(), bytes.size());
    len = bytes.size();
    return Load::Ok;
  }
  bool save(const uint8_t *data, size_t len) override {
    ++saves;
    if (failSaves > 0) {
      --failSaves;
      return false;
    }
    bytes.assign(reinterpret_cast<const char *>(data), len);
    return !saveLiesFailed;
  }
};
