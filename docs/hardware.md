# Hardware

This template does not assume a specific sensor or instrument.

Expected layers:

1. Device firmware using the BardBox compact protocol.
2. Pi driver that parses and normalizes readings.
3. Pi backend that applies freshness policy.
4. Dashboard that displays status badges and null values correctly.

Firmware development uses VS Code + PlatformIO. Devices report readings; the Pi
decides whether a node is stale or unavailable.
