# Tests

This template includes freshness tests for the BardBox node API contract.

The starter tests verify:

- fresh readings return `ok`
- transport failures return `node_unavailable`
- parse failures return `error`
- stale readings return `stale`
- stale/unavailable readings contain `null` data values
- cached readings are not presented as live after a failure
- valid new-format UIDs pass validation
- malformed new-format UIDs are rejected
- legacy `bb-0001` style IDs are supported but not considered the new standard
- configured legacy UID aliases normalize readings to canonical UIDs

Run:

```bash
python3 -m unittest discover -s tests
```

Firmware-specific host/layout/configuration tests and the counter/42 demo
receiver tests live with [Reference firmware](https://github.com/bardphysicslab/BardBox-Reference/tree/main/software/firmware).
This suite covers the remaining template server and tooling.
