#pragma once

// BardBoxSerial settings. Values come from the project's include/config.h
// (Serial section) when present; otherwise these documented defaults apply.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// Serial baud rate in bit/s.
#ifndef BARDBOX_SERIAL_BAUD
#define BARDBOX_SERIAL_BAUD 115200
#endif

static_assert(BARDBOX_SERIAL_BAUD >= 1200 && BARDBOX_SERIAL_BAUD <= 2000000,
              "BARDBOX_SERIAL_BAUD must be between 1200 and 2000000 bit/s");
