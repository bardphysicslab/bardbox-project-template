# BardBoxMeasurements

Sensor and measurement registration, publish checks and the latest-reading cache.

| Contract | Overview |
| --- | --- |
| Responsibility | Registers sensors and their ordered measurements; checks each `sensor.publish` (value count, declared type, finiteness); keeps the latest-reading cache; gives a consistent snapshot under its lock |
| Interface | `addSensor`, `addMeasurement(sensor, name, unit, type)`, `freeze()`, `publish(sensor, values, count)`, `snapshotChannels(out, capturedMs)` (implements the Buffer's `SnapshotSource`) |
| Declaration order | Measurements of different sensors may be declared in any interleaving; each sensor's own declaration order is its positional publish order |
| Types | FLOAT by default; `MeasurementType::Integer` and `::Boolean` (spelling proposed). An integer passed to FLOAT is the one conversion; a fractional value never becomes an Integer; nothing is zeroed or clamped |
| Outcomes | A wrong value count rejects the publish and leaves the cache unchanged. A bad channel is cached as null with `invalid_type`, `non_finite` or `not_representable`; healthy channels are kept |
| Time and identity | Each publish gets a per-sensor `publishSeq` and a hand-off time. These describe the publish call only: they are **not** acquisition identity or acquisition time, and republished old values get a new sequence. Acquisition time is unknown in M1 (binding is an open decision) |
| Not in M1 | Per-measurement bounds; freshness limits; an acquisition-time binding |
| Dependencies | The record types in `BardBoxBuffer`; FreeRTOS mutex on ESP32 |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.
