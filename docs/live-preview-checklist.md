# Live preview implementation checklist

This is implementation guidance, not an implemented runtime capability.
Canonical contract: [Optional Web Node live previews](https://github.com/bardphysicslab/bardbox/blob/codex/live-preview-standard/docs/web-node-live-preview.md).
After the standards PR merges, update this reference to main.

## Responsibilities to implement

- Node: persist an immutable record and stable ID; enqueue once; preview latest during backlog; independently drain bounded FIFO history; dequeue only on archival acknowledgment.
- Transport: dedicated preview operation with explicit capability support and safe legacy fallback. Authenticate previews using the project transport policy.
- Server: keep current measurement selection, current operational status and archival acceptance separate. Never write previews to measurement CSVs or double-count derived calculations.
- Archive: restart-safe deduplication including crash between append and acceptance-index update; maintain one FIFO writer and document clock anomalies.
- Backup: coordinate stable file versions with writers and completion status; reselect changed historical files; verify exactly copied versions before retention.
- UI: current measurements alongside a fresh buffer count/usage and catch-up indication; expire stale progress.

## Illustrative flow (not executable code)

At 12:00, a record with identity R enters flash and is previewed.
The dashboard displays R; the archival queue is still delivering 10:00 records.
Each history acknowledgment removes only the corresponding oldest record.
When R reaches the queue head, archive it once and acknowledge it durably.
The preview acknowledgment never removes R.
When the queue empties, normal archival delivery supplies current values.

## Validation checklist

- [ ] Old firmware continues with existing payloads and endpoints.
- [ ] New firmware safely falls back on an old server or unsupported endpoint.
- [ ] Sampling and persistence continue during backlog; neither upload path starves.
- [ ] Fresh measurement appears within one preview interval plus display refresh.
- [ ] Fresh buffer progress updates without old measurement/status regression.
- [ ] Live/history overlap and lost ACK produce one archived record.
- [ ] Reboots preserve IDs and queues; server crashes reconcile archival acceptance.
- [ ] Preview expiry/server restart preserve stale/null semantics.
- [ ] Cross-day, equal timestamp, invalid clock and rollback behavior is documented and tested.
- [ ] Confidence/QA advances only on archival acceptance, once per record.
- [ ] Concurrent backup and catch-up completion copy a coherent version.
- [ ] Manifest/retention never claims verification for a changed version.
- [ ] Missing/expired catch-up state cannot silently defer backups forever.
- [ ] Relevant host tests and firmware builds pass; physical validation is recorded separately.

Reusable code helpers and executable tests should be extracted from the reviewed CESH implementation into the appropriate template boundaries. Do not copy CESH sensor logic or its Drive script into unrelated projects, and do not advertise this capability until runtime implementation and validation are complete.
