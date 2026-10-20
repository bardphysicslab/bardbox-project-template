#include "BardBoxSettings.h"

#include <string.h>

namespace bbcore {

namespace {

const uint8_t kMagic[4] = {'B', 'B', 'S', '1'};

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool printable(const char *s, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x20 || c == 0x7f) return false;  // UTF-8 bytes >= 0x80 are allowed
  }
  return true;
}

template <size_t N>
void put(char (&field)[N], const char *s, size_t len) {
  memcpy(field, s, len);
  field[len] = 0;
}

void putU32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

uint32_t getU32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool BardBoxSettings::validUid(const char *s, size_t len) {
  if (!s || len < 1 || len > 32) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '.' || c == '-'))
      return false;
  }
  return true;
}

bool BardBoxSettings::validSsid(const char *s, size_t len) { return s && len <= 32 && printable(s, len); }

bool BardBoxSettings::validPassword(const char *s, size_t len) {
  return s && (len == 0 || (len >= 8 && len <= 63)) && printable(s, len);
}

// Whole-record rule, checked on load and before every save.
static bool consistent(const SettingsValues &v, BardBoxSettings::Field &bad) {
  if (!BardBoxSettings::validUid(v.uid, strlen(v.uid))) bad = BardBoxSettings::Field::Uid;
  else if (!BardBoxSettings::validSsid(v.ssid, strlen(v.ssid))) bad = BardBoxSettings::Field::Ssid;
  else if (!BardBoxSettings::validPassword(v.password, strlen(v.password)) || (!v.ssid[0] && v.password[0]))
    bad = BardBoxSettings::Field::Password;
  else return true;
  return false;
}

BardBoxSettings::Field BardBoxSettings::fieldNamed(const char *name) {
  if (!name) return Field::Unknown;
  if (strcmp(name, "uid") == 0) return Field::Uid;
  if (strcmp(name, "wifi_ssid") == 0) return Field::Ssid;
  if (strcmp(name, "wifi_password") == 0) return Field::Password;
  return Field::Unknown;
}

const char *BardBoxSettings::fieldName(Field field) {
  switch (field) {
    case Field::Uid: return "uid";
    case Field::Ssid: return "wifi_ssid";
    case Field::Password: return "wifi_password";
    default: return "unknown";
  }
}

void BardBoxSettings::begin(SettingsStore *store, const char *defaultUid, const char *defaultSsid,
                            const char *defaultPassword) {
  store_ = store;
  open_ = false;
  defaults_ = SettingsValues();
  const char *uid = defaultUid && validUid(defaultUid, strlen(defaultUid)) ? defaultUid : "unassigned";
  put(defaults_.uid, uid, strlen(uid));
  const char *ssid = defaultSsid ? defaultSsid : "";
  const char *password = defaultPassword ? defaultPassword : "";
  Field bad;
  SettingsValues wifi = defaults_;
  if (strlen(ssid) < sizeof wifi.ssid && strlen(password) < sizeof wifi.password) {
    put(wifi.ssid, ssid, strlen(ssid));
    put(wifi.password, password, strlen(password));
    if (consistent(wifi, bad)) defaults_ = wifi;  // otherwise: no Wi-Fi default
  }
  reload();
  active_ = saved_;
}

void BardBoxSettings::reload() {
  saved_ = defaults_;
  revision_ = 0;
  if (!store_) {
    storeState_ = StoreState::Unavailable;
    return;
  }
  uint8_t buf[kMaxRecord];
  size_t len = 0;
  const SettingsStore::Load got = store_->load(buf, sizeof buf, len);
  if (got == SettingsStore::Load::None) {
    storeState_ = StoreState::None;
  } else if (got == SettingsStore::Load::Ok && decode(buf, len, saved_, revision_)) {
    storeState_ = StoreState::Saved;
  } else {
    saved_ = defaults_;  // unreadable or invalid: ignored in favour of the defaults
    revision_ = 0;
    storeState_ = StoreState::Invalid;
  }
}

bool BardBoxSettings::pending() const {
  return strcmp(active_.uid, saved_.uid) != 0 || strcmp(active_.ssid, saved_.ssid) != 0 ||
         strcmp(active_.password, saved_.password) != 0;
}

// "BBS1", revision (u32 LE), then uid, ssid, password each as a u8 length and
// bytes, then CRC-32 (zlib, LE) over everything before it.
size_t BardBoxSettings::encode(const SettingsValues &v, uint32_t revision, uint8_t *out) const {
  size_t n = 0;
  memcpy(out, kMagic, 4);
  n += 4;
  putU32(out + n, revision);
  n += 4;
  const char *fields[3] = {v.uid, v.ssid, v.password};
  for (int i = 0; i < 3; ++i) {
    const size_t len = strlen(fields[i]);
    out[n++] = static_cast<uint8_t>(len);
    memcpy(out + n, fields[i], len);
    n += len;
  }
  putU32(out + n, crc32(out, n));
  return n + 4;
}

bool BardBoxSettings::decode(const uint8_t *buf, size_t len, SettingsValues &out, uint32_t &revision) const {
  if (len < 4 + 4 + 3 + 4 || len > kMaxRecord || memcmp(buf, kMagic, 4) != 0) return false;
  if (getU32(buf + len - 4) != crc32(buf, len - 4)) return false;
  SettingsValues v;
  size_t at = 8;
  char *fields[3] = {v.uid, v.ssid, v.password};
  const size_t caps[3] = {sizeof v.uid, sizeof v.ssid, sizeof v.password};
  for (int i = 0; i < 3; ++i) {
    if (at >= len - 4) return false;
    const size_t n = buf[at++];
    if (n >= caps[i] || at + n > len - 4) return false;
    memcpy(fields[i], buf + at, n);
    fields[i][n] = 0;
    if (memchr(fields[i], 0, n)) return false;
    at += n;
  }
  Field bad;
  if (at != len - 4 || !consistent(v, bad)) return false;
  out = v;
  revision = getU32(buf + 4);
  return true;
}

BardBoxSettings::Result BardBoxSettings::beginUpdate(uint32_t expectedRevision, uint32_t nowMs) {
  if (!store_) return Result::Unavailable;
  if (expectedRevision != revision_) return Result::StaleRevision;
  candidate_ = saved_;
  open_ = true;
  lastTouchMs_ = nowMs;
  return Result::Ok;
}

BardBoxSettings::Result BardBoxSettings::checkOpen(uint32_t nowMs) {
  if (!open_) return Result::NoTransaction;
  if (nowMs - lastTouchMs_ > kTransactionTimeoutMs) {
    open_ = false;
    return Result::Expired;
  }
  lastTouchMs_ = nowMs;
  return Result::Ok;
}

BardBoxSettings::Result BardBoxSettings::setField(Field field, const char *value, size_t len, uint32_t nowMs) {
  const Result r = checkOpen(nowMs);
  if (r != Result::Ok) return r;
  if (field == Field::Unknown) {
    open_ = false;
    return Result::UnknownField;
  }
  bool ok = value && !memchr(value, 0, len);
  if (ok && field == Field::Uid && (ok = validUid(value, len))) put(candidate_.uid, value, len);
  if (ok && field == Field::Ssid && (ok = validSsid(value, len))) put(candidate_.ssid, value, len);
  if (ok && field == Field::Password && (ok = validPassword(value, len))) put(candidate_.password, value, len);
  if (ok) return Result::Ok;
  open_ = false;
  return Result::InvalidValue;
}

BardBoxSettings::Result BardBoxSettings::rejectValue(uint32_t nowMs) {
  const Result r = checkOpen(nowMs);
  if (r != Result::Ok) return r;
  open_ = false;
  return Result::InvalidValue;
}

BardBoxSettings::Result BardBoxSettings::commit(uint32_t nowMs, Field &invalidField) {
  const Result r = checkOpen(nowMs);
  if (r != Result::Ok) return r;
  open_ = false;
  invalidField = Field::Unknown;
  if (!consistent(candidate_, invalidField)) return Result::InvalidValue;
  uint8_t buf[kMaxRecord];
  const uint32_t intended = revision_ + 1;
  const SettingsValues wanted = candidate_;
  const size_t len = encode(wanted, intended, buf);
  store_->save(buf, len);
  // Report what the store holds now, not what save() returned: a failed save
  // normally leaves the previous record, which reload then shows.
  reload();
  const bool saved = storeState_ == StoreState::Saved && revision_ == intended &&
                     strcmp(saved_.uid, wanted.uid) == 0 && strcmp(saved_.ssid, wanted.ssid) == 0 &&
                     strcmp(saved_.password, wanted.password) == 0;
  return saved ? Result::Ok : Result::StoreFailed;
}

}  // namespace bbcore
