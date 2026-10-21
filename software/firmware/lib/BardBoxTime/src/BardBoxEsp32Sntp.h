#pragma once

#include "BardBoxTime.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace bbcore {
// ESP32 SNTP provider (lwIP SNTP via configTime, UTC). Device-lifetime singleton:
// the synchronization callback must never target a destroyed object.
TimeSyncProvider &defaultEsp32SntpProvider();
}
#endif
