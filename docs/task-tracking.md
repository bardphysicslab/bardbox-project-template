# Task tracking

Follow the canonical [BardBox task synchronization policy](https://github.com/bardphysicslab/bardbox/blob/codex/task-sync/docs/task-synchronization.md)
while its draft is under review; use the main-branch policy after adoption.

Every actionable GitHub issue needs one linked Trello card on the designated
project board. Preserve human priority/ownership and use explicit issue identity,
not title matching. Review duplicates, archived matches, missing status labels
and concurrent changes before updating either side. Do not copy the status map
into app or firmware code.

BardBox Tools provides an offline audit, bounded live-read adapters, a durable
journal and a disabled-by-default Trello writer in its task-sync branch. These are
building blocks for reviewed actions, not configured unattended synchronization.
Two-sided conflict tracking, reviewed identity adoption, credentials, board/list
mappings and live validation still require a separate setup. This template does
not enable automatic external mutations or copy the shared implementation.
