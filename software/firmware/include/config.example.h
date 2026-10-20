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
// Network (BardBoxNetwork) -- delivery not implemented yet (M1)
// ---------------------------------------------------------------------------

// Agreed default: network delivery on (1); 0 makes a serial-only device. M1
// only reports this setting; it does not connect or deliver.
#define BARDBOX_NETWORK_ENABLED 1

// Wi-Fi SSID, password, endpoint and upload credentials are private: they
// belong only in the ignored local config.h or runtime provisioning, never in
// this file, and have no defaults.

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
