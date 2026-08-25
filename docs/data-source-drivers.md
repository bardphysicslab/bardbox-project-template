# Data Source Drivers

This template follows the canonical BardBox data-source driver standard.

Every sensor, instrument, node, or remote/vendor feed should be represented to the application through the same normalized driver boundary. Local serial devices, BardBox nodes, PurpleAir/QuantAQ-style remote feeds, and other configured sources are not separate application architectures.

Drivers implement:

```python
class SensorDriver:
    def get_info(self) -> dict: ...
    def get_capabilities(self) -> dict: ...
    def get_reading(self) -> dict: ...
```

Keep vendor commands, API calls, CSV/archive parsing, and transport quirks inside the driver or its bounded acquisition helper. `main.py` should consume normalized readings only.

## Availability

Use one semantic model for every source:

```text
LIVE -> STALE -> OFFLINE
```

Normalized statuses are `ok`, `stale`, `error`, and `node_unavailable`. The UI maps `node_unavailable` to OFFLINE.

Source-specific numeric thresholds are allowed because expected update cadence differs by transport. A cloud source may tolerate a longer gap than a directly polled serial instrument. However:

- stale means the last valid data is too old to present as current
- offline means the source has exceeded its configured availability window or cannot be reached
- a source must not remain stale forever
- `offline_after` must be greater than `stale_after`
- stale/offline readings must not expose cached metric values as live data

Where useful, expose `expected_update_interval_s`, `stale_after_s`, `offline_after_s`, and `last_seen` in driver metadata/`extended`.

See the canonical `bardbox/docs/data-source-drivers.md` standard before adding a new source integration.
