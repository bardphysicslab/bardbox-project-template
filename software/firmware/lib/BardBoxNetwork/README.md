# BardBoxNetwork

Network configuration and status.

| Contract | Overview |
| --- | --- |
| Responsibility | Owns the network section of the configuration and reports network status |
| Configuration | `BARDBOX_NETWORK_ENABLED` (0 or 1, default 1 per the agreed default). SSID, password, endpoint and credentials are private and have no defaults; M1 reads none of them |
| Status | `disabled` or `not_implemented`. M1 does **not** connect, deliver or acknowledge; that is a later milestone |
| Dependencies | None |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **M1 progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.
