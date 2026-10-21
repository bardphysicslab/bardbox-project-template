#pragma once
// Host stub: the SNTP calls BardBoxEsp32Sntp makes; the test fires the callback.
#include <stdint.h>
struct timeval;
extern void (*g_sntpCallback)(struct timeval *);
inline void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval *)) { g_sntpCallback = cb; }
inline void sntp_set_sync_interval(uint32_t) {}
