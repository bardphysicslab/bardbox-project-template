# Device Instructions

Template firmware uses the BardBox compact protocol and the current node UID
standard. The canonical specifications live in the separate `bardbox` repo:

- [Device Instructions](https://github.com/bardphysicslab/bardbox/blob/main/docs/device-instructions.md)
- [BardBox Web Node Protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md)

Do not copy the complete platform protocol into a project repository. Web Node
projects should reference the canonical document and keep only
implementation-specific payload, endpoint, hardware, and deployment details
locally.

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

## Web Node Implementation Checklist

For a network-uploading node:

- implement core `INFO`, `HEADER`, and `READ` commands;
- implement `PING`, `START`, and `STOP` only where useful;
- optionally expose `UPLOAD`, `PAYLOAD`, and `BUFFER` diagnostics;
- treat `BUFFER_CLEAR` as destructive maintenance that discards
  unacknowledged data, never automatic recovery;
- persist readings before upload whenever practical and expose
  `buffer_count` and `buffer_max_records`;
- remove a buffered record only after successful acknowledgment; HTTP/HTTPS
  acknowledgment means any `2xx` response;
- retry oldest-first while continuing to sample and append new readings;
- expose connectivity, recent communication/storage errors, and buffer state
  through `INFO`;
- report firmware and protocol versions independently;
- keep automatic upload and catch-up quiet in production, reserving verbose
  output for an explicit operator diagnostic or development build;
- do not add `TRACE` or `CATCHUP_TRACE` as standard commands.
