#pragma once

#include <stdint.h>

namespace bbcore {

// Connectivity diagnostics, owned and refreshed by the Network owner and read by
// Serial INFO and the delivery body's "device_status". Both use the same field
// names and meanings (template#36); only the encoding differs (INFO key=value,
// JSON). An empty string or rssiKnown=false means unavailable: IP and RSSI are
// shown only while connected, never as stale values. No password, token or URL
// path, query or credentials ever appear here.
enum class WifiState : uint8_t { Disabled, Unconfigured, Disconnected, Connected };

inline const char *wifiStateName(WifiState state) {
  switch (state) {
    case WifiState::Disabled: return "disabled";          // network delivery off
    case WifiState::Unconfigured: return "unconfigured";  // no valid endpoint/CA/SSID: Wi-Fi not started
    case WifiState::Disconnected: return "disconnected";
    case WifiState::Connected: return "connected";
  }
  return "unknown";
}

struct ConnectivityStatus {
  WifiState wifi = WifiState::Disabled;
  char ip[16] = {};         // station IPv4 address, while connected
  char mac[18] = {};        // station MAC (aa:bb:cc:dd:ee:ff), once Wi-Fi has started
  bool rssiKnown = false;   // RSSI only while connected
  int16_t rssiDbm = 0;
  char ssid[33] = {};       // effective SSID this boot (saved setting or default)
  char receiver[160] = {};  // configured upload endpoint as scheme://host[:port] only
};

}  // namespace bbcore
