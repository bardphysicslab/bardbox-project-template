# Embedded OTA verification reference

Status: verification helpers, not a complete updater. Including these headers
does not update a device. Delivery, JSON parsing, persistent assignment state,
inactive-slot writing and boot validation still require platform integration.

`BardBoxOTAManifest.h` validates metadata and emits exactly the Python signing
tool's ASCII/LF message. Host sanitizer tests compare actual C++ output against
Python, reject malformed values and oversized images, enforce component/target/
layout/configuration/queue compatibility, and check completed-image size and hash.

`BardBoxOTAVerifyESP32.h` uses ESP32 Arduino 2.x / mbedTLS 2.x to verify ECDSA
P-256 signatures. It bounds signature/key input, requires provisioned trust,
checks the public key curve and enforces local compatibility. It compiled and
linked against the cached Feather ESP32-S3 framework. Signature execution on a
physical device remains untested. Other framework versions require validation.

Run `pio run -d tests/esp32_ota_compile` for the compile/link check, pinned to
PlatformIO espressif32 7.0.1. It is a build fixture, not deployment firmware.

## Installer responsibilities

1. Parse bounded JSON strictly: exact fields, no duplicates, and integer size
   without coercion/truncation. The C++ struct cannot detect JSON parser mistakes.
2. Populate `OTATarget` from trusted local configuration and actual slot capacity.
   Resolve the signing key ID only against provisioned public keys, never a key
   downloaded with the release.
3. Require `verifyOTAESP32(...) == OTAVerification::Valid` before accepting the
   release for download/install. Signature failure must stop installation.
4. Stream to the inactive slot with verified TLS, finite deadlines, size bounds
   and SHA-256 calculation. Also validate ESP image format and chip suitability.
5. Require `matchesOTAImage` and platform image validation before choosing the
   boot partition. A signed manifest does not prove downloaded bytes are correct.
6. Persist generation, event sequence and pending assignment before reboot.
   Confirm only after bounded local acquisition/storage/configuration checks.
   Preserve the queue and its compatibility during rollback.

Server device registrations now require an explicit `component`, such as `app`,
matching the manifest and device configuration. Update draft OTA configuration
accordingly. OTA stays disabled unless configured; measurement APIs are unchanged.
