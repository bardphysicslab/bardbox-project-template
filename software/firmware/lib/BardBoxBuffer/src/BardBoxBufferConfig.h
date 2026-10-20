#pragma once

// BardBoxBuffer settings. Values come from the project's include/config.h
// (Buffer section) when present; otherwise these documented defaults apply.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// Recording interval in seconds: how often a completed record is created and
// admitted to buffering. Not sensor sampling and not network catch-up timing.
// The default is an implementation choice, not a universal sensor cadence.
#ifndef BARDBOX_RECORDING_INTERVAL_S
#define BARDBOX_RECORDING_INTERVAL_S 60
#endif

// Persistent queue capacity, in records. At capacity the oldest
// unacknowledged record is kept as an anchor and the next oldest is evicted
// (counted as data loss). Physical flash capacity is not verified.
#ifndef BARDBOX_BUFFER_CAPACITY_RECORDS
#define BARDBOX_BUFFER_CAPACITY_RECORDS 256
#endif

// Records held in RAM while storage is unavailable or a write failed; they are
// retried in order. When this is full the newest record is dropped (counted).
#ifndef BARDBOX_BUFFER_RAM_RECORDS
#define BARDBOX_BUFFER_RAM_RECORDS 8
#endif

static_assert(BARDBOX_RECORDING_INTERVAL_S >= 5 && BARDBOX_RECORDING_INTERVAL_S <= 86400,
              "BARDBOX_RECORDING_INTERVAL_S must be between 5 and 86400 seconds");
static_assert(BARDBOX_BUFFER_RAM_RECORDS >= 1 && BARDBOX_BUFFER_RAM_RECORDS <= 64,
              "BARDBOX_BUFFER_RAM_RECORDS must be between 1 and 64");
static_assert(BARDBOX_BUFFER_CAPACITY_RECORDS >= 2 && BARDBOX_BUFFER_CAPACITY_RECORDS <= 1024,
              "BARDBOX_BUFFER_CAPACITY_RECORDS must be between 2 and 1024");
