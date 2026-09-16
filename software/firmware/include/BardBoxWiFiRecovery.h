#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace bardbox {

// ESP32 Wi-Fi recovery for nodes that own a Wi-Fi connection. Keep sensor
// acquisition and serial commands in the main loop; this helper never waits
// for association. Project code must keep any outbound queue durable before
// enabling restartAfterMs.
struct WiFiRecoveryConfig {
  uint32_t retryIntervalMs = 30000;
  uint32_t restartAfterMs = 900000;  // 0 disables software restart.
};

class WiFiRecovery {
 public:
  explicit WiFiRecovery(WiFiRecoveryConfig config = {}) : config_(config) {}

  template <typename OnDisconnect, typename BeforeRestart>
  void service(const char *ssid, const char *password, uint32_t nowMs,
               OnDisconnect onDisconnect, BeforeRestart beforeRestart) {
    if (WiFi.status() == WL_CONNECTED) {
      if (offline_) {
        Serial.println("WIFI,RECOVERED");
      }
      offline_ = false;
      restartIssued_ = false;
      return;
    }

    if (!offline_) {
      offline_ = true;
      offlineSinceMs_ = nowMs;
      attempted_ = false;
      onDisconnect();  // Close stale TCP/client sessions once per outage.
      Serial.println("WIFI,DISCONNECTED,automatic_recovery=1");
    }

    const uint32_t elapsed = nowMs - offlineSinceMs_;
    if (config_.restartAfterMs != 0 && elapsed >= config_.restartAfterMs) {
      if (!restartIssued_) {
        restartIssued_ = true;
        Serial.print("WIFI,RECOVERY_RESTART,offline_ms=");
        Serial.println(elapsed);
        beforeRestart();  // Project must flush/close any persistent queue.
        Serial.flush();
        ESP.restart();
      }
      return;
    }

    if (attempted_ && nowMs - lastAttemptMs_ < config_.retryIntervalMs) {
      return;
    }
    attempted_ = true;
    lastAttemptMs_ = nowMs;
    Serial.print("WIFI,AUTO_RETRY,offline_ms=");
    Serial.println(elapsed);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);  // ESP32 association proceeds asynchronously.
  }

  bool offline() const { return offline_; }
  uint32_t offlineMs(uint32_t nowMs) const {
    return offline_ ? nowMs - offlineSinceMs_ : 0;
  }

 private:
  WiFiRecoveryConfig config_;
  bool offline_ = false;
  bool attempted_ = false;
  bool restartIssued_ = false;
  uint32_t offlineSinceMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
};

}  // namespace bardbox
