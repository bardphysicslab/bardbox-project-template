# Task tracking

Follow the canonical [BardBox task synchronization policy](https://github.com/bardphysicslab/bardbox/blob/codex/task-sync/docs/task-synchronization.md)
while its draft is under review; use the main-branch policy after adoption.

Every actionable GitHub issue needs one linked Trello card on the designated
project board. Preserve human priority/ownership and use explicit issue identity,
not title matching. Review duplicates, archived matches, missing status labels
and concurrent changes before updating either side. Do not copy the status map
into app or firmware code.

BardBox Tools provides an offline audit, bounded live-read adapters, durable
journal/baselines/review-policy storage, reviewed adoption helpers and two-sided
planning in its task-sync branch. Both Trello and GitHub senders are available
but disabled by default. They are building blocks for reviewed actions.

Coordinating a complete run, credentials, reviewed board/list mappings and identity
classifications, migration and live validation remain separate setup work. Remote
edits after a source read are still a documented race. This template does not
enable automatic external mutations or copy the shared implementation.
