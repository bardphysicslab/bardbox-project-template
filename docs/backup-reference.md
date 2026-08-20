# Backup and Retention Reference

This component is required only when a deployment stores historical sensor files
locally and later removes them under a retention policy. It is not needed for a
stateless monitor.

Use CESH Air's verified-manifest backup as the canonical implementation. Adapt
only the readings root, archive destination, service names, and deployment-local
environment path. Preserve these invariants:

- acquire a non-blocking lock so timer and manual runs cannot overlap;
- recursively copy historical files without deleting remote archive history;
- upload active daily files, but verify only stable/closed versions;
- batch-check new or changed stable files after upload;
- atomically record exact relative path, size, and mtime only after verification;
- retain local files whenever copy, verification, manifest parsing, or state
  updates fail;
- delete after the configured retention period only when the exact current file
  version matches the verified manifest;
- keep backup destination and credentials outside application source code.

Do not use archive-destructive sync behavior. Do not add backup machinery until
the project has a well-defined historical readings root.
