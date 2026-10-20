# BardBoxInterface

The one student-facing BardBox object. It wires the owners together and implements none of them.

| Contract | Overview |
| --- | --- |
| Student API | `BardBoxInterface bardbox;`, `bardbox.addSensor(name)` returns a `BardBoxSensor` handle, `sensor.addMeasurement(name, unit[, type])`, `bardbox.begin()` (no arguments), `sensor.publish(values...)` with already-acquired values in declared order, optional `bardbox.getSetting("recording_interval_ms", value)`. No student `poll`, time feeding, record builder or tickets |
| Orchestration | Forwards registration and publish to `BardBoxMeasurements`; on ESP32 `begin()` starts one FreeRTOS service task that runs a bounded Serial step and a recording step every 10 ms, so service continues during `delay()`. A separate Network task advances an injected transport and exchanges requests/results with the service-task Buffer owner. ESP32 selects its Wi-Fi/HTTPS adapter when private settings are valid. Task priority and stack are candidates |
| Version | `FIRMWARE_VERSION` must be defined before the header and non-empty, or the build fails; `begin()` is defined only in student translation units, so there is one definition per build. Include the header from one file (`main.cpp`); other files that include it also need the version define with "Please set FIRMWARE_VERSION in main.cpp." Other placeholder values are not rejected yet (open) |
| Storage | On ESP32, `begin()` gives the Buffer the LittleFS adapter; hosts call `useStorage()` first. The Network owner uses the mailbox; `bufferForDelivery()` is only an owner-task test inspection hook |
| Configuration | `BARDBOX_DEVICE_UID` (default `unassigned`, the unprovisioned sentinel) and the Wi-Fi SSID/password are defaults; saved settings (`BardBoxSettings`, NVS on ESP32, `useSettingsStore()` on hosts) override them from the next start. INFO reports the active UID; new records carry it |
| Failures | If the cache mutex or the service task cannot be created, `begin()` returns false and no Serial service is promised. Registration errors make `begin()` return false and appear in `INFO reg_errors` |
| Dependencies | `BardBoxMeasurements`, `BardBoxBuffer`, `BardBoxSerial`, `BardBoxNetwork`, `BardBoxSettings` |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.
