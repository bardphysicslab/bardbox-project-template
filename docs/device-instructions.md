# Device Instructions

Template firmware uses the BardBox compact protocol and the current node UID
standard.

## UID Format

New node UIDs must use:

```text
bb-<site>-<type>-<instance>
```

Template example: `bb-prj-air-001`

Rules:

- prefix is always `bb`
- site code is exactly 3 lowercase letters
- type code is exactly 3 lowercase letters
- instance is exactly 3 digits with leading zeros
- UID is immutable once deployed

Legacy IDs like `bb-0001` remain supported for existing deployments, but are
deprecated. New projects created from this template should use the new format.

## Legacy UID Aliasing

During migration, a driver config can map device-reported legacy IDs to the
canonical UID:

```json
{
  "uid": "bb-gol-air-001",
  "legacy_uids": ["bb-0001", "rkc-01", "spn1-0001"]
}
```

API responses and dashboards use the canonical UID. New logs should use the
canonical UID from normalized API readings. Historical logs are left untouched.

## Commands

Required:

- `INFO`
- `HEADER`
- `READ`

Optional:

- `PING`
- `START` / `STOP` for streaming or session-style devices
