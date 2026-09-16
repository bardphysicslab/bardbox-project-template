# OTA reference implementation

Operator-page behavior tests: `node --test tests/firmware_ui.test.cjs` verifies
refresh ordering and release-selection controls using a small DOM test double.
These checks do not replace visual browser review.

For device-side validation helpers and remaining installer responsibilities, see
[embedded verification](ota-device-verification.md).

Development status: server and shared installer implemented; CESH integration is
a bench candidate. Physical commissioning and acceptance remain required. Do not register existing single-slot nodes as
OTA-ready. The authoritative contract is bardbox/docs/ota-update-standard.md.

## Enable the management service

Install requirements-ota.txt in the application environment. Set
BARDBOX_OTA_CONFIG to a protected, deployment-local JSON file outside the source
tree and measurement data directories. No environment variable means no OTA routes.
The config is intentionally separate from app configuration reports.

Required structure (replace all placeholder values locally):

```json
{
  "enabled": true,
  "public_origin": "https://your-bardbox-host.example",
  "state_directory": "/var/lib/bardbox/ota",
  "admins": {"lab-operator": "SHA256_OF_RANDOM_OPERATOR_TOKEN"},
  "public_keys": {"lab-1": "P256_PUBLIC_KEY_PEM"},
  "devices": {
    "bb-cesh-air-001": {
      "token_sha256": "SHA256_OF_UNIQUE_RANDOM_DEVICE_TOKEN",
      "target": "cesh-bme280",
      "layout": "dual-2m-v1",
      "config_schema": "1",
      "queue_schema": "1",
      "slot_bytes": 2097152,
      "component": "app"
    }
  }
}
```

Generate independent high-entropy random tokens (at least 32 random bytes).
Store only their SHA-256 hashes server-side. The browser login uses the operator
name and token as its password; each device receives only its own token. Signing
private keys stay with the release operator, never in the server configuration.
Serve only over HTTPS with an HTTP-to-HTTPS redirect, access controls/rate limits
at the reverse proxy, and no CORS on /ota. Keep request size limits at or below
3 MiB and request timeouts bounded. Do not expose the development HTTP server.

Open /firmware to upload a signed manifest and its firmware binary. Compatible
registered nodes can then be assigned one at a time. Unprepared devices must not
be registered with an eligible layout. The server cannot verify physical layout;
the installer must check actual hardware and partition sizes before installing.

## Signing format

Use scripts/sign_firmware.py with --image, --metadata, --key and --output.
Metadata contains format=bardbox-ota-v1, release_id, component, version, target,
layout, config_schema, queue_schema and key_id. The tool supplies image size and
SHA-256. It prompts for a password if the private PEM key is encrypted.

Signature algorithm: ECDSA P-256 with SHA-256; ASN.1 DER signature encoded as
standard Base64. Signed bytes are ASCII field values in this exact order, each
terminated by LF (including the last field): format, release_id, component,
version, target, layout, config_schema, queue_schema, size, sha256, key_id.
Size is an unsigned decimal integer. All text fields accept only ASCII letters,
digits, underscore, period and hyphen, up to 96 characters. Digest is 64 lowercase
hex characters. A signature authenticates the metadata AND the image digest.
Firmware must use this exact encoding and have cross-language test vectors before
deployment. Application-level signature checks do not imply hardware secure boot.

## Status and retry contract

Devices poll /ota/v1/device/assignment using a unique bearer token. A 204 means
there is no active delivery. A response contains generation, signed envelope and
a same-origin artifact path. Devices must never forward tokens to another origin.
They persist assignment generation and status event sequence across reboot. Event
sequence increases within an assignment even across boots. Repeating an identical
event is safe; old/conflicting events cannot change the current outcome.

Report status to /ota/v1/device/status with generation, sequence, boot_id, state,
running_version, running_sha256, bytes and failure. Running digest is SHA-256 of
the exact release artifact bytes, not an ELF digest or ESP image appended hash.
Implementations must compute/persist that distinction correctly. Confirm only
after local health checks, using the actual running version and digest. Terminal
outcomes cannot be changed to an in-progress state; retries need a new assignment.

Operator assignments carry a request_id retained by the browser after an uncertain
response so retries cannot accidentally issue a new generation. Stopping delivery
does not revoke bytes already downloaded; final device outcomes remain accepted.
Contact freshness is separate from installation outcome. An offline device is
not automatically a failed update. Existing measurement APIs do not change.

## Persistence and operations

SQLite here holds OTA releases, assignments, events and operator audit history;
it does not replace the measurement archive. Back up this database consistently
and preserve generations when restoring; never restore an older generation history
over a fleet without an explicit reconciliation procedure. Release blobs and
metadata are inserted in one transaction. Release IDs are immutable.

Pilot v1 retains event/audit history. Monitor disk space and back up before any
future retention pruning. Keep the service disabled until commissioning checks
pass. Browser and bench validation, live configuration, key provisioning, compact
queue integration and device installation are separate unfinished gates.
