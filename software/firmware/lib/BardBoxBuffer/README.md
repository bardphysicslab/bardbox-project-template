# BardBoxBuffer

Recording interval, record creation and admission.

| Contract | Overview |
| --- | --- |
| Responsibility | Owns the recording interval and its boundaries; at each due boundary takes one snapshot from a `SnapshotSource`, creates one full-node record and admits it; keeps the latest completed record |
| Interface | `begin(nowMs, bootId)`, `service(nowMs, source)` (at most one record per step), `latest(record)`, `buffered()`, `backpressureCount()`, `skippedBoundaries()`, `recordingIntervalMs()` |
| Configuration | `BARDBOX_RECORDING_INTERVAL_S` (seconds, default 60, valid 5..86400): record creation and admission only, not sampling or network catch-up. The default is an implementation choice. `BARDBOX_BUFFER_RAM_RECORDS` (default 8, 1..64) |
| Behavior | Boundaries are monotonic (previous due time plus the interval); a late step records once and counts the missed boundaries. The latest completed record is set when the record is created, before admission. A full queue gives counted backpressure. A channel whose sensor never published is recorded as `no_sample`. A channel with the same hand-off as the previous record is marked as a repeated hand-off (not yet on the wire) |
| Not in M1 | **Persistent storage**: M1 keeps records in RAM only, lost on restart. No FIFO acknowledgement, saturation anchor, restart recovery or freshness judgement |
| Dependencies | None (defines the shared record schema and the `SnapshotSource` interface) |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **M1 progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.
