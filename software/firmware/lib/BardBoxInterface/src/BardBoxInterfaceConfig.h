#pragma once

// BardBoxInterface settings. Values come from the project's include/config.h
// (Identity section) when present; otherwise these documented defaults apply.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// Device UID reported in INFO. "unassigned" is the explicit unprovisioned
// sentinel, never a valid assigned UID. A UID is not authentication.
#ifndef BARDBOX_DEVICE_UID
#define BARDBOX_DEVICE_UID "unassigned"
#endif

static_assert(sizeof(BARDBOX_DEVICE_UID) <= 33, "BARDBOX_DEVICE_UID must be at most 32 characters");
