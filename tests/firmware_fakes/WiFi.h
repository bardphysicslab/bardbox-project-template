#pragma once

constexpr int WL_CONNECTED = 3;
constexpr int WL_DISCONNECTED = 6;
constexpr int WIFI_STA = 1;

struct FakeWiFi {
  int state = WL_DISCONNECTED;
  int attempts = 0;
  int modeCalls = 0;
  int status() const { return state; }
  void mode(int) { ++modeCalls; }
  void begin(const char *, const char *) { ++attempts; }
};
extern FakeWiFi WiFi;
