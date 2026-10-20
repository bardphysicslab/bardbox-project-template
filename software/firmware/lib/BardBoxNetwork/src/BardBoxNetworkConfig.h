#pragma once

// BardBoxNetwork settings. Values come from the project's include/config.h
// (Network section) when present; otherwise these documented defaults apply.
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

// Agreed default: network delivery on (1) or off (0, serial-only).
#ifndef BARDBOX_NETWORK_ENABLED
#define BARDBOX_NETWORK_ENABLED 1
#endif

// The upload endpoint, Wi-Fi SSID/password and upload credential are private:
// define them only in the ignored local config.h (or provision them). They
// have no defaults. Without an endpoint the network reports "unconfigured".
//   #define BARDBOX_NETWORK_ENDPOINT "https://..."
#if defined(BARDBOX_NETWORK_ENDPOINT)
#define BARDBOX_NETWORK_HAS_ENDPOINT 1
#else
#define BARDBOX_NETWORK_HAS_ENDPOINT 0
#endif

// Finite deadline for each delivery phase, in milliseconds. A phase that does
// not finish in time is abandoned and the record stays queued.
#ifndef BARDBOX_NETWORK_ASSOCIATION_TIMEOUT_MS
#define BARDBOX_NETWORK_ASSOCIATION_TIMEOUT_MS 15000
#endif
#ifndef BARDBOX_NETWORK_DNS_TIMEOUT_MS
#define BARDBOX_NETWORK_DNS_TIMEOUT_MS 5000
#endif
#ifndef BARDBOX_NETWORK_CONNECT_TIMEOUT_MS
#define BARDBOX_NETWORK_CONNECT_TIMEOUT_MS 10000
#endif
#ifndef BARDBOX_NETWORK_TLS_TIMEOUT_MS
#define BARDBOX_NETWORK_TLS_TIMEOUT_MS 10000
#endif
#ifndef BARDBOX_NETWORK_REQUEST_TIMEOUT_MS
#define BARDBOX_NETWORK_REQUEST_TIMEOUT_MS 10000
#endif
#ifndef BARDBOX_NETWORK_RESPONSE_TIMEOUT_MS
#define BARDBOX_NETWORK_RESPONSE_TIMEOUT_MS 10000
#endif

// Retry backoff after a failed attempt: doubles from the base up to the cap,
// and resets after an accepted delivery. Not catch-up pacing.
#ifndef BARDBOX_NETWORK_RETRY_BASE_MS
#define BARDBOX_NETWORK_RETRY_BASE_MS 5000
#endif
#ifndef BARDBOX_NETWORK_RETRY_MAX_MS
#define BARDBOX_NETWORK_RETRY_MAX_MS 300000
#endif

// How often an idle network checks the queue for a record to deliver.
#ifndef BARDBOX_NETWORK_IDLE_POLL_MS
#define BARDBOX_NETWORK_IDLE_POLL_MS 1000
#endif

static_assert(BARDBOX_NETWORK_ENABLED == 0 || BARDBOX_NETWORK_ENABLED == 1,
              "BARDBOX_NETWORK_ENABLED must be 0 or 1");
#define BARDBOX_NETWORK_TIMEOUT_OK(t) ((t) >= 100 && (t) <= 120000)
static_assert(BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_ASSOCIATION_TIMEOUT_MS) &&
                  BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_DNS_TIMEOUT_MS) &&
                  BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_CONNECT_TIMEOUT_MS) &&
                  BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_TLS_TIMEOUT_MS) &&
                  BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_REQUEST_TIMEOUT_MS) &&
                  BARDBOX_NETWORK_TIMEOUT_OK(BARDBOX_NETWORK_RESPONSE_TIMEOUT_MS),
              "BARDBOX_NETWORK_*_TIMEOUT_MS must be between 100 and 120000 ms");
static_assert(BARDBOX_NETWORK_RETRY_BASE_MS >= 100 &&
                  BARDBOX_NETWORK_RETRY_BASE_MS <= BARDBOX_NETWORK_RETRY_MAX_MS &&
                  BARDBOX_NETWORK_RETRY_MAX_MS <= 3600000,
              "Need 100 <= BARDBOX_NETWORK_RETRY_BASE_MS <= BARDBOX_NETWORK_RETRY_MAX_MS <= 3600000");
static_assert(BARDBOX_NETWORK_IDLE_POLL_MS >= 100 && BARDBOX_NETWORK_IDLE_POLL_MS <= 60000,
              "BARDBOX_NETWORK_IDLE_POLL_MS must be between 100 and 60000 ms");

// Wi-Fi connection settings belong to this module. The default network is the
// open BardWireless network (maintainer decision, template#36 comment 6304409557);
// a project config.h that defines BARDBOX_WIFI_SSID/PASSWORD (even empty) wins,
// and settings saved over USB serial override both from the next start.
#ifndef BARDBOX_WIFI_SSID
#define BARDBOX_WIFI_SSID "BardWireless"
#endif
#ifndef BARDBOX_WIFI_PASSWORD
#define BARDBOX_WIFI_PASSWORD ""
#endif
// Endpoint, trusted CA PEM and optional bearer token have NO example values or
// defaults: BARDBOX_NETWORK_ENDPOINT, BARDBOX_NETWORK_CA_PEM,
// BARDBOX_NETWORK_BEARER_TOKEN. A private network password belongs only in the
// ignored local config.h or in saved settings.
// Only HTTPS with peer/hostname verification is supported. Absent/invalid
// configuration reports unconfigured; no insecure TLS fallback exists.
#ifndef BARDBOX_WIFI_RECONNECT_MS
#define BARDBOX_WIFI_RECONNECT_MS 30000
#endif
static_assert(BARDBOX_WIFI_RECONNECT_MS >= 1000 && BARDBOX_WIFI_RECONNECT_MS <= 3600000,
              "BARDBOX_WIFI_RECONNECT_MS must be 1000..3600000 milliseconds");
