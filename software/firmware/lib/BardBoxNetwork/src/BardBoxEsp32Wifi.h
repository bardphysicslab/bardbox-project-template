#pragma once
#include "BardBoxWifiTransport.h"
#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace bbcore {
// Device-lifetime singleton: asynchronous DNS callbacks must never target a
// destroyed object. Factory performs no association or socket I/O. The SSID
// and password are this boot's effective values (saved settings or compiled
// defaults); they must outlive the transport. Endpoint, CA and token stay
// compiled project configuration. The first call fixes the values.
WifiHttpsTransport &defaultEsp32WifiTransport(const char *ssid, const char *password);
}
#endif
