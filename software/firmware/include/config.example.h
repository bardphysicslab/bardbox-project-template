#pragma once

// BardBox firmware configuration: tracked, safe example.
//
// Copy this file to include/config.h (ignored by Git via **/config.h) and edit
// the local copy. Never put real credentials in this tracked example.
//
// STATUS: NOT CONSUMED. No source file includes this header or config.h yet.
// The current simulator (src/main.cpp) hardcodes its values, so editing these
// settings changes nothing until a module explicitly #includes "config.h" and
// reads them. Each module will read and validate its own section.

// ---------------------------------------------------------------------------
// Serial (BardBoxSerial)
// ---------------------------------------------------------------------------

// Serial baud rate, in bit/s. Matches the current simulator, which hardcodes
// Serial.begin(115200) in src/main.cpp (not read from here yet).
// platformio.ini monitor_speed sets only the computer's serial monitor and
// must match this value; it does not set Serial.begin.
#define BARDBOX_SERIAL_BAUD 115200

// ---------------------------------------------------------------------------
// Network (BardBoxNetwork) -- not implemented
// ---------------------------------------------------------------------------

// Agreed default: Wi-Fi/network delivery on. No network module exists yet, so
// no setting is defined here. Wi-Fi SSID, password, endpoint and upload
// credentials are private: they belong only in the ignored local config.h or
// runtime provisioning, never in this file, and have no defaults.

// ---------------------------------------------------------------------------
// Buffer (BardBoxBuffer) -- not implemented
// ---------------------------------------------------------------------------

// Agreed default: persistent recording on. No Buffer module exists yet, so no
// capacity or limit setting is defined here.
