#pragma once
#define WIFI_STA 1
#define WL_CONNECTED 3
struct StubWifi {
 bool up = true;
 void persistent(bool) {}
 void setAutoReconnect(bool) {}
 bool mode(int) { return true; }
 void begin(const char *, const char *) { up = true; }
 int status() { return up ? WL_CONNECTED : 0; }
};
extern StubWifi WiFi;
