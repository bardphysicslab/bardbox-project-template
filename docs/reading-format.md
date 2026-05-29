# Reading Format

Template API readings follow the BardBox standard:

```json
{
  "uid": "bb-prj-air-001",
  "timestamp": "2026-05-29T19:23:16Z",
  "status": "ok",
  "message": "Fresh valid reading",
  "data": {
    "temp_c": 21.6,
    "rh_pct": 43.0,
    "press_pa": 100980
  },
  "extended": {
    "last_seen": "2026-05-29T19:23:16Z"
  },
  "raw": null
}
```

Statuses are `ok`, `stale`, `error`, and `node_unavailable`. Stale,
unavailable, and error readings use `null` data values so dashboards do not
show cached readings as live.

New UIDs must use `bb-<site>-<type>-<instance>`, such as `bb-prj-air-001`.
Legacy `bb-0001` style IDs are supported only for existing deployments.

If a device still reports a legacy UID, configure `legacy_uids` on the driver
entry. The API normalizes readings to the canonical UID, which is what the
dashboard displays and what new logs should write. Old logs are not rewritten.
