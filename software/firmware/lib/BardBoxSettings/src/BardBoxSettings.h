#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxSettingsStore.h"

namespace bbcore {

// Per-device settings that change without a rebuild: UID, Wi-Fi SSID and
// password. BardBoxSettings owns their validation, persistence and the
// saved-versus-active distinction. Compiled values (config.h) are the
// defaults; a saved record overrides all three. Saved changes activate at the
// next start: active values never change while running, so records made in
// this boot keep this boot's UID and earlier records keep their own.
//
// Field rules (a subset of the BardBox DB1 rules, limited by the record's
// stored attribution):
//   uid            1..32 of [A-Za-z0-9_.-]
//   wifi_ssid      0..32 bytes, no control characters; empty = no Wi-Fi
//   wifi_password  empty (open network) or 8..63 bytes, no control
//                  characters; set-only, never reported
// An empty SSID requires an empty password.
struct SettingsValues {
  char uid[33] = {};
  char ssid[33] = {};
  char password[64] = {};
};

class BardBoxSettings {
 public:
  static constexpr uint32_t kTransactionTimeoutMs = 60000;
  static constexpr size_t kMaxRecord = 4 + 4 + 3 + 32 + 32 + 63 + 4;

  enum class StoreState : uint8_t { Unavailable, None, Saved, Invalid };
  enum class Field : uint8_t { Uid, Ssid, Password, Unknown };
  enum class Result : uint8_t {
    Ok, Unavailable, StaleRevision, NoTransaction, Expired, UnknownField, InvalidValue, StoreFailed
  };

  // Loads the saved record (if any) and fixes this boot's active values.
  // store may be null (nothing can be saved). Invalid compiled defaults are
  // replaced by "unassigned" / no Wi-Fi, never used.
  void begin(SettingsStore *store, const char *defaultUid, const char *defaultSsid,
             const char *defaultPassword);

  // This boot's values.
  const SettingsValues &active() const { return active_; }
  // What the next boot will use: the saved record, or the defaults.
  const SettingsValues &next() const { return saved_; }
  StoreState storeState() const { return storeState_; }
  uint32_t revision() const { return revision_; }  // 0: nothing saved
  bool pending() const;                            // next differs from active

  static Field fieldNamed(const char *name);
  static const char *fieldName(Field field);
  static bool validUid(const char *s, size_t len);
  static bool validSsid(const char *s, size_t len);
  static bool validPassword(const char *s, size_t len);

  // Update transaction: begin against the revision the tool read, set
  // fields, commit. Commit validates the whole candidate and saves it as one
  // record; on failure the previous saved record stays in effect. A begin
  // replaces an unfinished transaction; idle transactions expire. A refused
  // field or value ends the transaction, so a partial update cannot commit.
  Result beginUpdate(uint32_t expectedRevision, uint32_t nowMs);
  Result setField(Field field, const char *value, size_t len, uint32_t nowMs);
  Result commit(uint32_t nowMs, Field &invalidField);
  Result rejectValue(uint32_t nowMs);  // a value that could not be decoded
  void abort() { open_ = false; }

 private:
  Result checkOpen(uint32_t nowMs);
  bool decode(const uint8_t *buf, size_t len, SettingsValues &out, uint32_t &revision) const;
  size_t encode(const SettingsValues &values, uint32_t revision, uint8_t *out) const;
  void reload();

  SettingsStore *store_ = nullptr;
  SettingsValues defaults_;
  SettingsValues active_;
  SettingsValues saved_;
  SettingsValues candidate_;
  StoreState storeState_ = StoreState::Unavailable;
  uint32_t revision_ = 0;
  bool open_ = false;
  uint32_t lastTouchMs_ = 0;
};

}  // namespace bbcore
