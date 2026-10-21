#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxRecord.h"
#include "BardBoxTimeConfig.h"

namespace bbcore {

// Synchronization provider (ESP32: SNTP; hosts: a fake). Injected, so the
// Time owner holds the policy and a later RTC adapter can sit beside it.
class TimeSyncProvider {
 public:
  virtual ~TimeSyncProvider() {}
  // Starts background synchronization once; must not block.
  virtual void start(const char *server, uint32_t refreshMs) = 0;
  // Current UTC only after at least one successful synchronization this boot.
  virtual bool utcNow(int64_t &epochMs) = 0;
  virtual uint32_t syncCount() = 0;           // successful synchronizations this boot
  virtual uint32_t lastSyncMonotonicMs() = 0;  // millis() at the last one (valid if syncCount)
};

enum class TimeState : uint8_t {
  Unavailable,  // no synchronization provider in this build
  Unsynced,     // provider present, no successful synchronization yet this boot
  Synced,       // UTC valid
};
const char *timeStateName(TimeState state);

// Bounded snapshot for Serial INFO and the delivery body's device_status.
struct TimeStatus {
  TimeState state = TimeState::Unavailable;
  uint32_t syncs = 0;
  bool lastSyncKnown = false;
  uint32_t lastSyncAgeS = 0;  // seconds since the last successful synchronization
  bool recordTimestamps = BARDBOX_TIME_RECORD_TIMESTAMPS != 0;  // new records get UTC
  char utc[25] = "";  // current clock (not a record time), empty unless synced
};

// The one owner of UTC (template#49): starts NTP once Wi-Fi is connected,
// refreshes it in the background, and answers UTC queries for record creation
// and for the TLS clock gate. Before a successful synchronization UTC is
// unknown; it is never invented, and monotonic time stays the basis for
// scheduling and elapsed-time diagnostics. Records keep the UTC they were
// created with.
class BardBoxTime : public UtcSource {
 public:
  void begin(TimeSyncProvider *provider, const char *server = BARDBOX_TIME_NTP_SERVER,
             uint32_t refreshMs = static_cast<uint32_t>(BARDBOX_TIME_REFRESH_S) * 1000UL);
  // Bounded step: starts synchronization the first time the network is up.
  void service(bool networkConnected);
  bool utcNow(int64_t &epochMs, UtcSourceKind &source) override;
  bool synced();
  TimeStatus status(uint32_t nowMs);

 private:
  TimeSyncProvider *provider_ = nullptr;
  const char *server_ = BARDBOX_TIME_NTP_SERVER;
  uint32_t refreshMs_ = 0;
  bool started_ = false;
};

// "YYYY-MM-DDTHH:MM:SS.mmmZ" (24 characters + NUL); false if cap < 25 or out of range.
bool formatUtc(int64_t epochMs, char *out, size_t cap);

}  // namespace bbcore
