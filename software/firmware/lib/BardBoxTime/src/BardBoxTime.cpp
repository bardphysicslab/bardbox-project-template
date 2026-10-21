#include "BardBoxTime.h"

#include <stdio.h>

namespace bbcore {

const char *timeStateName(TimeState state) {
  switch (state) {
    case TimeState::Unavailable: return "unavailable";
    case TimeState::Unsynced: return "unsynced";
    case TimeState::Synced: return "synced";
  }
  return "unknown";
}

void BardBoxTime::begin(TimeSyncProvider *provider, const char *server, uint32_t refreshMs) {
  provider_ = provider;
  server_ = server;
  refreshMs_ = refreshMs;
  started_ = false;
}

void BardBoxTime::service(bool networkConnected) {
  // SNTP needs the network stack; start it once, when Wi-Fi first connects.
  if (provider_ && !started_ && networkConnected) {
    provider_->start(server_, refreshMs_);
    started_ = true;
  }
}

bool BardBoxTime::utcNow(int64_t &epochMs, UtcSourceKind &source) {
  if (!provider_ || !provider_->utcNow(epochMs)) return false;
  source = UtcSourceKind::Ntp;
  return true;
}

bool BardBoxTime::synced() {
  int64_t ms;
  return provider_ && provider_->utcNow(ms);
}

TimeStatus BardBoxTime::status(uint32_t nowMs) {
  TimeStatus s;
  if (!provider_) return s;
  s.syncs = provider_->syncCount();
  s.state = synced() ? TimeState::Synced : TimeState::Unsynced;
  if (s.syncs) {
    s.lastSyncKnown = true;
    s.lastSyncAgeS = (nowMs - provider_->lastSyncMonotonicMs()) / 1000;
  }
  return s;
}

// Civil date from days since 1970-01-01 (Howard Hinnant's algorithm), so no
// libc time-zone state is involved.
bool formatUtc(int64_t epochMs, char *out, size_t cap) {
  if (cap < 25 || epochMs < 0 || epochMs > 253402300799999LL) return false;  // through 9999-12-31
  const int64_t secs = epochMs / 1000, days = secs / 86400, sod = secs % 86400;
  const int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
  const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
  snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", static_cast<int>(y), static_cast<int>(m),
           static_cast<int>(d), static_cast<int>(sod / 3600), static_cast<int>(sod / 60 % 60),
           static_cast<int>(sod % 60), static_cast<int>(epochMs % 1000));
  return true;
}

}  // namespace bbcore
