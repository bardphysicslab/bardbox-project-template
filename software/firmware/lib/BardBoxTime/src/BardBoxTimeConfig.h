#pragma once

// BardBoxTime settings, from the project's include/config.h (Time section)
// when present, otherwise these defaults.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// NTP server. The earlier Network setting BARDBOX_NETWORK_NTP_SERVER is still
// honoured when BARDBOX_TIME_NTP_SERVER is not set.
#ifndef BARDBOX_TIME_NTP_SERVER
#if defined(BARDBOX_NETWORK_NTP_SERVER)
#define BARDBOX_TIME_NTP_SERVER BARDBOX_NETWORK_NTP_SERVER
#else
#define BARDBOX_TIME_NTP_SERVER "pool.ntp.org"
#endif
#endif

// UTC timestamps on new records (1, default) or not (0). Off affects only
// record creation: Time, NTP, the system clock and HTTPS certificate checks
// keep running, and records keep their ID, boot and monotonic order. Applies
// when a record is created; saved records keep what they were created with.
#ifndef BARDBOX_TIME_RECORD_TIMESTAMPS
#define BARDBOX_TIME_RECORD_TIMESTAMPS 1
#endif
static_assert(BARDBOX_TIME_RECORD_TIMESTAMPS == 0 || BARDBOX_TIME_RECORD_TIMESTAMPS == 1,
              "BARDBOX_TIME_RECORD_TIMESTAMPS must be 0 or 1");

// How often a synchronized clock is refreshed over NTP, in seconds.
#ifndef BARDBOX_TIME_REFRESH_S
#define BARDBOX_TIME_REFRESH_S 3600
#endif
static_assert(BARDBOX_TIME_REFRESH_S >= 60 && BARDBOX_TIME_REFRESH_S <= 86400,
              "BARDBOX_TIME_REFRESH_S must be 60..86400 seconds");
