#pragma once
#define WIFI_STA 1
#define WL_CONNECTED 3
struct IPAddress {
 unsigned char b[4];
 unsigned char operator[](int i) const { return b[i]; }
};
struct StubWifi {
 bool up = true;
 void persistent(bool) {}
 void setAutoReconnect(bool) {}
 bool mode(int) { return true; }
 void begin(const char *, const char *) { up = true; }
 int status() { return up ? WL_CONNECTED : 0; }
 IPAddress localIP() { return IPAddress{{192, 0, 2, 10}}; }
 int RSSI() { return -61; }
};
extern StubWifi WiFi;
