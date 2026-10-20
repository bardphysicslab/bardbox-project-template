#pragma once
#include "BardBoxWifiTransport.h"
#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace bbcore {
// Device-lifetime singleton: asynchronous DNS callbacks must never target a
// destroyed object. Factory performs no association or socket I/O.
WifiHttpsTransport &defaultEsp32WifiTransport();
}
#endif
