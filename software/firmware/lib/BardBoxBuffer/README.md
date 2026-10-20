# BardBoxBuffer

Recording interval, record creation, durable queue and acknowledgement.

| Contract | Overview |
| --- | --- |
| Responsibility | Owns the recording interval and boundaries; at each due boundary takes one snapshot from a `SnapshotSource`, creates one full-node record, persists it, and keeps the latest completed record. Owns FIFO delivery order and acknowledgement |
| Interface | `begin(nowMs, bootId, storage)`, `service(nowMs, source)` (at most one RAM retry and one new record per step), `peekOldest(record, token)`, `acknowledge(token)` (`Removed`, `StaleToken`, `Empty`, `StorageError`), `latest(record)`, `status()` |
| Storage | `StorageAdapter` interface (`BardBoxStorage.h`): `LittleFsStorage` on ESP32 (the `spiffs`-labelled data partition, mounted with format-on-fail off); an in-memory fake on hosts. One immutable file per record (`/bbq/r<seq>`), written to a temp file and renamed; `/bbq/meta` holds the next queue sequence and the eviction count |
| Framing | Storage frame `BBQ1` | frame version | length | CRC-32 | payload, separate from any serial wire form. The payload carries its own schema version (1 = M1 candidate, not an accepted scientific schema) |
| Configuration | `BARDBOX_RECORDING_INTERVAL_S` (seconds, default 60, 5..86400): record creation and admission only. `BARDBOX_BUFFER_CAPACITY_RECORDS` (default 256, 2..1024). `BARDBOX_BUFFER_RAM_RECORDS` (RAM retry slots, default 8, 1..64) |
| Guarantees (host-tested) | Records and FIFO order survive a restart unchanged (original boot ID, record number, values). Only the exact head token removes a record; stale tokens remove nothing. Queue tokens are not reused, even if the meta file is lost. Write, rename or remove failures keep the record (RAM retry, or still the head). Interrupted writes are removed on recovery and counted; unreadable records are quarantined (`/bbq/q<seq>`), counted and never delivered. A mount failure leaves storage unavailable and never formats. At capacity the oldest unacknowledged record stays as the anchor and the next oldest is evicted, counted and persisted |
| Limits | Physical flash durability, power-loss timing and wear are **not verified**; one file per record has unmeasured LittleFS overhead, so the default capacity is not a measured retention figure. The latest completed record is per boot (none after a restart until the next boundary). No freshness is judged. Write-failure counts are per boot. Records beyond a reduced capacity stay on storage but are not delivered. Storage writes run on the service task and may delay Serial |
| Dependencies | None beyond its own `StorageAdapter`; defines the shared record schema and `SnapshotSource` |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md); [buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md).

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values and simulated storage/restarts; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.
