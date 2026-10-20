#pragma once

// BardBox firmware configuration: tracked, safe example.
//
// Copy this file to include/config.h (ignored by Git via **/config.h) and edit
// the local copy. Never put real credentials in this tracked example.
//
// STATUS: read by the BardBox modules (lib/) when this file is copied to
// include/config.h. Any setting left out uses the module's documented default,
// and each module validates its own section at build time.

// ---------------------------------------------------------------------------
// Serial (BardBoxSerial)
// ---------------------------------------------------------------------------

// Serial baud rate, in bit/s. platformio.ini monitor_speed sets only the
// computer's serial monitor and must match this value.
#define BARDBOX_SERIAL_BAUD 115200

// ---------------------------------------------------------------------------
// Identity (BardBoxInterface)
// ---------------------------------------------------------------------------

// Device UID reported in INFO. "unassigned" is the unprovisioned sentinel.
#define BARDBOX_DEVICE_UID "unassigned"

// ---------------------------------------------------------------------------
// Network (BardBoxNetwork) -- ESP32 Wi-Fi/HTTPS; hardware validation pending
// ---------------------------------------------------------------------------

// Agreed default: network delivery on (1); 0 makes a serial-only device.
// Without a valid endpoint, SSID and trusted CA this reports unconfigured.
// The real ESP32 adapter connects only when those private settings are supplied.
#define BARDBOX_NETWORK_ENABLED 1

// Fill these in only in your ignored local config.h, never in this tracked file.
// Empty required fields leave delivery unconfigured; they are not working values.
#define BARDBOX_WIFI_SSID ""             // Your Wi-Fi network name.
#define BARDBOX_WIFI_PASSWORD ""         // Your password; empty for an open network.
#define BARDBOX_NETWORK_ENDPOINT ""      // Full HTTPS upload URL from your receiver.

// Trusted CA certificate in PEM format, provided by the receiver operator.
// Use a single C string with escaped newlines, or adjacent quoted lines with
// a trailing backslash on EVERY continued macro line. Do not put a multiline
// raw string directly in a #define: the preprocessor ends it at the first newline.
#define BARDBOX_NETWORK_CA_PEM ""

// Upload access token, if required by your receiver (required for the 42 demo).
// Optional for receivers that do not require one. Never use insecure TLS.
#define BARDBOX_NETWORK_BEARER_TOKEN ""

// Deadline/backoff settings and bounds are documented in
// lib/BardBoxNetwork/src/BardBoxNetworkConfig.h; omitted values use those defaults.

// ---------------------------------------------------------------------------
// Buffer (BardBoxBuffer) -- persistent queue on LittleFS (host-tested only)
// ---------------------------------------------------------------------------

// Recording interval, in seconds: how often a completed record is created and
// admitted to the buffer. Not sensor sampling and not network catch-up timing.
// Valid 5..86400. The default is an implementation choice, not a universal
// sensor cadence.
#define BARDBOX_RECORDING_INTERVAL_S 60

// Persistent queue capacity, in records (2..1024). At capacity the oldest
// unacknowledged record is kept and the next oldest is evicted (counted loss).
#define BARDBOX_BUFFER_CAPACITY_RECORDS 256

// Records held in RAM while storage is unavailable or a write failed (1..64).
#define BARDBOX_BUFFER_RAM_RECORDS 8
