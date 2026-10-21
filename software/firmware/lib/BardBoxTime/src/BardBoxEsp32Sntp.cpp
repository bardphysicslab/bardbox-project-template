#include "BardBoxEsp32Sntp.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace bbcore {
namespace {
portMUX_TYPE syncLock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t syncs = 0, lastSyncMs = 0;

// Runs in the lwIP task after each successful synchronization.
void onSync(struct timeval *) {
  portENTER_CRITICAL(&syncLock);
  ++syncs;
  lastSyncMs = millis();
  portEXIT_CRITICAL(&syncLock);
}

class Esp32Sntp : public TimeSyncProvider {
 public:
  void start(const char *server, uint32_t refreshMs) override {
    sntp_set_time_sync_notification_cb(onSync);
    sntp_set_sync_interval(refreshMs);
    configTime(0, 0, server);  // UTC; background SNTP, retries on its own
  }
  bool utcNow(int64_t &epochMs) override {
    if (syncCount() == 0) return false;  // never trust the clock before a sync this boot
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    epochMs = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return true;
  }
  uint32_t syncCount() override {
    portENTER_CRITICAL(&syncLock);
    const uint32_t n = syncs;
    portEXIT_CRITICAL(&syncLock);
    return n;
  }
  uint32_t lastSyncMonotonicMs() override {
    portENTER_CRITICAL(&syncLock);
    const uint32_t t = lastSyncMs;
    portEXIT_CRITICAL(&syncLock);
    return t;
  }
};
}  // namespace

TimeSyncProvider &defaultEsp32SntpProvider() {
  static Esp32Sntp provider;
  return provider;
}
}  // namespace bbcore
#endif
