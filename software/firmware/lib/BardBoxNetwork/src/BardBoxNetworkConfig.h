#pragma once

// BardBoxNetwork settings. Values come from the project's include/config.h
// (Network section) when present; otherwise these documented defaults apply.
// Wi-Fi SSID, password, endpoint and credentials are private and have no
// defaults; they are not read by M1.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// Agreed default: Wi-Fi/network delivery on (1) or off (0, serial-only).
#ifndef BARDBOX_NETWORK_ENABLED
#define BARDBOX_NETWORK_ENABLED 1
#endif

static_assert(BARDBOX_NETWORK_ENABLED == 0 || BARDBOX_NETWORK_ENABLED == 1,
              "BARDBOX_NETWORK_ENABLED must be 0 or 1");
