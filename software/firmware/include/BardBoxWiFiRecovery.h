#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace bardbox {

// ESP32 Wi-Fi recovery for nodes that own a Wi-Fi connection. Keep sensor
// acquisition and serial commands in the main loop; this helper never waits
// for association. Project code must keep any outbound queue durable before
// enabling restartAfterMs.
struct WiFiRecoveryConfig {
  uint32_t retryIntervalMs;
  uint32_t restartAfterMs;  // 0 disables software restart.
  bool verbose;  // Keep unsolicited production serial output quiet.
  WiFiRecoveryConfig(uint32_t retry = 30000, uint32_t restart = 900000,
                     bool diagnostics = false)
      : retryIntervalMs(retry), restartAfterMs(restart), verbose(diagnostics) {}
};

class WiFiRecovery {
 public:
  explicit WiFiRecovery(WiFiRecoveryConfig config = {}) : config_(config) {}

  template <typename OnDisconnect, typename BeforeRestart>
  void service(const char *ssid, const char *password, uint32_t nowMs,
               OnDisconnect onDisconnect, BeforeRestart beforeRestart) {
    if (WiFi.status() == WL_CONNECTED) {
      if (offline_) {
        if (config_.verbose) Serial.println("WIFI,RECOVERED");
      }
      offline_ = false;
      restartIssued_ = false;
      restartAttempted_ = false;
      return;
    }

    if (!offline_) {
      offline_ = true;
      offlineSinceMs_ = nowMs;
      attempted_ = false;
      onDisconnect();  // Close stale TCP/client sessions once per outage.
      if (config_.verbose) Serial.println("WIFI,DISCONNECTED,automatic_recovery=1");
    }

    const uint32_t elapsed = nowMs - offlineSinceMs_;
    if (config_.restartAfterMs != 0 && elapsed >= config_.restartAfterMs &&
        !restartIssued_ &&
        (!restartAttempted_ || nowMs - lastRestartAttemptMs_ >= config_.retryIntervalMs)) {
      restartAttempted_ = true;
      lastRestartAttemptMs_ = nowMs;
      // Return false if durable state or the project's persistent restart budget
      // is not safe. A veto must leave normal reconnects running.
      if (beforeRestart()) {
        restartIssued_ = true;
        if (config_.verbose) {
          Serial.print("WIFI,RECOVERY_RESTART,offline_ms=");
          Serial.println(elapsed);
          Serial.flush();
        }
        ESP.restart();
        return;
      }
    }

    if (attempted_ && nowMs - lastAttemptMs_ < config_.retryIntervalMs) {
      return;
    }
    attempted_ = true;
    lastAttemptMs_ = nowMs;
    if (config_.verbose) {
      Serial.print("WIFI,AUTO_RETRY,offline_ms=");
      Serial.println(elapsed);
    }
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
  bool restartAttempted_ = false;
  uint32_t lastRestartAttemptMs_ = 0;
  uint32_t offlineSinceMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
};

}  // namespace bardbox
