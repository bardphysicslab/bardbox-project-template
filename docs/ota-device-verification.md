# Embedded OTA verification reference

The [HTTPS installer components](ota-installer-reference.md) connect these helpers
to verified delivery and inactive-slot writes; project worker integration remains.

For the optional two-slot layout and device preparation requirements, see
[partition commissioning](ota-partition-commissioning.md).

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

### Exact artifact identity after restart

The signed SHA-256 covers every byte of the `.bin` file, including its appended
ESP image digest. The bundled SDK documents that `esp_partition_get_sha256()`
returns that appended digest for hashed ESP applications, rather than hashing the
entire downloaded file. Both generated CESH sensor images confirm these differ.

`BardBoxOTAImageDigestESP32.h` reads exactly the supplied artifact length from an
application partition and calculates the full SHA-256 in steps of at most 1,024
bytes. Schedule/yield between steps to preserve acquisition. Read/hash failure
must stop identity-dependent OTA operations. This helper compiled and linked;
physical flash-read validation remains pending.

The persistent state format is now `OS2`, recording `candidateBytes` and
`previousBytes` alongside the hashes. `accept()` and `booted()` require the byte
length as well as the full artifact hash. Initial commissioning must establish
the known-good artifact's exact length/hash. After reboot, hash the running
partition over the recorded candidate extent and, if needed, the previous extent;
accept an identity only if both length and hash match. Do not guess the length
from partition capacity or silently substitute an ESP app-description digest.
Unknown earlier state formats fail closed; they are not reset automatically.

`BardBoxOTAAssignment.h` parses the reference server's response into a bounded
assignment. It rejects duplicate/unknown/missing fields, non-integer or overflowing
numbers, JSON escapes, non-ASCII strings, bodies larger than 4,096 bytes, and paths
other than the assigned release's relative artifact path. It accepts JSON whitespace
and arbitrary field order. Generation is limited to positive 32-bit values. The
server emits unescaped ASCII for these fields; general-purpose JSON is not needed.
Parser success is not signature verification: pass its manifest and signature to
the verifier with provisioned trust before any install action. Resolve the relative
artifact path against the configured origin and disable redirects.

Persistent-state building blocks are `BardBoxOTAState.h`, `BardBoxOTAStateRecord.h`
and `BardBoxOTAStateStoreESP32.h`. The state machine calls a persistence callback
before permitting download or pending-boot actions. The bounded versioned record
has a CRC; the ESP32 adapter stores one complete blob in NVS namespace `bb-ota`
and verifies readback. Missing/corrupt state fails closed. `provisionIdle()` is an
explicit commissioning operation, never automatic startup recovery.

Restore verified state before accepting assignments. An interrupted download is
recorded as failed after restart; the same generation cannot reinstall. A boot of
the previous image while pending/validating records rollback. Equal-image new
assignments can be confirmed without writing an image or rebooting. Unknown
persistence outcomes disable update actions until reload. Acquisition remains an
independent responsibility and must continue when OTA is disabled.

Use `reserveStatus()` before a new progress event and reuse the exact event on
network retry; coalesce progress to bound NVS wear. `booted()` reserves a fresh
sequence before reporting from a new boot. Only call `confirm()` after local checks
and the platform's successful mark-app-valid operation; neither the state helper
nor the NVS adapter performs bootloader operations. These are shared components,
connected to optional CESH bench builds, not yet deployed. Host tests cover uncertain commits,
reboots and corrupt/truncated records; physical NVS power-loss tests remain pending.

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


## Arduino startup acceptance

For an Arduino ESP32 installer that owns boot validation, define a strong C-linkage
`verifyRollbackLater()` returning `true` in exactly one compiled translation unit.
The framework's weak default otherwise marks a pending image valid during
`initArduino()`, before project setup and storage/acquisition checks. Verify the
final ELF resolves the hook as a strong symbol and test rollback on hardware;
checking SDK configuration alone is insufficient. CESH is the current consumer;
projects without this installer must not adopt the override without a validator.

Complete queue mounting and index recovery before publishing initial storage
health to the validator. A lazily initialized readiness flag is not evidence of
broken storage. Exercise empty and non-empty queue startup, metadata-write failure,
and retention of the oldest queued record. CESH's first signed bench image exposed
both of these integration gaps; host helper tests alone had not covered startup.
