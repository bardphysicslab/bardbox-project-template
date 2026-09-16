# USB device configuration reference

Status: implemented reference and host/build validation; physical commissioning
and OTA rollout are not yet validated. This is an optional Bardbox capability.

## What it does

Each device keeps its name, UID, Wi-Fi settings, telemetry credentials and trusted
update settings in NVS (the ESP32's persistent settings store). Firmware updates
can then use a common image without embedding those per-device secrets. CESH is
the first consumer. The shared source of truth is this template's
`BardBoxDeviceConfig*.h`, `BardBoxProvisionTransfer.h` and `scripts/provision_device.py`.
Other projects, including RKC, do not acquire unused provisioning code.

The configuration remains a boot-time snapshot. Writing it does not change a
running measurement session or request a reboot. The first record may be written
only to an empty store. Repeating the identical record is safe; replacing another
record, erasing it, or resetting OTA state is not supported by this initial tool.
Missing/corrupt settings are never repaired by formatting or seeding defaults.

## Preparation

Keep a copy of the device's UID, queued data, known-good firmware, partition table
and bootloader before any separately authorized USB firmware preparation. Maintain
its original UID: older queue rows may derive identity from runtime configuration.
Use a firmware build that implements these commands. An older deployed image will
reject them and needs deliberate USB preparation first.

Install `requirements-provisioning.txt` in a Python environment. Copy
`docs/device-provisioning.example.json` into ignored `.provisioning/` and fill it
privately. Put the CA bundle and signing **public** key alongside it; never put a
signing private key on a device. The CA bundle must cover both telemetry and OTA
origins. Keep private inputs outside commits/backups that are shared publicly.
Empty Wi-Fi password means open Wi-Fi; secured Wi-Fi supports 8–63-byte passwords.
Enterprise Wi-Fi and raw 64-hex PSKs are outside this initial pilot contract.

Run a local check first:

```sh
python scripts/provision_device.py --config .provisioning/node.json \
  --running-firmware /private/path/to/exact-installed-firmware.bin --dry-run
```

For deliberate bench commissioning, add `--port` with the device's USB serial port
and remove `--dry-run`. The script does not flash, erase or request a reboot. USB
port opening can have board/driver-specific reset behavior; verify this on the
bench. A success means the configuration was saved and read back. It becomes active
on the next boot; it does not prove Wi-Fi reachability, OTA eligibility or rollback.
The node independently hashes its running firmware's exact supplied byte extent
before accepting the initial record, catching a mismatched binary file.

If a reply is lost, inspect `PROVISION_STATUS`. Re-running the same configuration
and exact running firmware is allowed. Do not erase NVS to resolve a failed write.
If an existing different/corrupt record blocks provisioning, retain it and use a
separately designed maintenance/recovery procedure.

## Format, transport and failure behavior

`DC1` is a maximum 8,192-byte binary record: magic, little-endian 32-bit initial
artifact length, twelve little-endian 16-bit-length-prefixed UTF-8 fields, then
CRC32. Field order: UID, name, SSID, password, telemetry URL/token, OTA origin/token,
CA PEM, signing key ID/public PEM, initial full-artifact SHA256. Decode into a
candidate, validate every field and checksum, then publish the complete object.
TLS deadlines are fixed reference defaults rather than provisioned policy.

USB commands are `PROVISION_BEGIN <size>`, `PROVISION_DATA <offset> <hex>`,
`PROVISION_COMMIT`, `PROVISION_CANCEL`, `PROVISION_STATUS`. Chunks contain at most
16 bytes and fit the existing 64-byte command buffer. Ordered identical chunk
retries are accepted; gaps, changed duplicates, malformed hex and oversized input
are rejected. A 30-second idle or 180-second total staging timeout drops the
uncommitted RAM record. No partial record is saved. Oversized serial lines must
be discarded through their delimiter so a suffix cannot execute as another command.

The NVS adapter uses namespace `bb-device`, key `config`, separate from the queue
and `bb-ota`. Public signing keys must parse as P-256 and CA material must parse.
NVS write and readback must match. CRC detects accidental corruption, not malicious
writes; this reference does not enable flash/NVS encryption or secure boot.
Physical USB access is a trusted commissioning boundary. Never log record bytes,
credential fields or raw serial exceptions. Certificate/key/Wi-Fi rotation remains
an explicit maintenance follow-up; it is not silently enabled through this tool.

## Validation and limits

Host tests cover record corruption/truncation, field bounds, open Wi-Fi, chunk
replay/order, timeout/wraparound, Python-to-C++ record decoding, bounded serial
commands and redacted errors. Tests of the actual NVS adapter against explicit storage/crypto API doubles cover
rejected trust material, failed writes, uncertain readback, identical retry and
refusal to replace different/corrupt records. They do not validate real crypto or
flash. SDK compile/link covers actual NVS and key/CA APIs.
Physical NVS power interruption, flash capacity/wear, USB behavior, live TLS,
network reconnection and acquisition timing still require bench tests.

Choice recorded: an initial write-once configuration with identical retry avoids
accidental identity changes and queue relabeling. A general configuration editor
would require atomic activation/rollback, credential rotation and maintenance
policy. Those capabilities are intentionally separate. Do not claim provisioning
alone completes an OTA installer or authorizes a deployment.

## Native USB CDC host-ready signal

Some ESP32-S3 native USB CDC builds reply only while DTR indicates a connected
host. On bench node004 this was verified with DTR asserted and RTS deasserted.
Use `--assert-dtr` only after checking the board reset behavior; default DTR/RTS
both remain deasserted. This option does not request a reboot or flash write.
