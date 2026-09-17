# Adopting compact offline storage

Status: optional reference components, tested on a computer and integrated locally
in CESH firmware 0.7.0. Physical flash validation is pending. These components do
not change the template's serial example or supply a complete persistent queue.

Canonical requirements: [compact storage](https://github.com/bardphysicslab/bardbox/blob/main/docs/compact-record-storage.md)
and [OTA compatibility](https://github.com/bardphysicslab/bardbox/blob/main/docs/ota-update-standard.md).

## Components

- `software/firmware/include/BardBoxRecordDelta.h`: lossless encoding of completed
  payloads, preserving numeric text and metadata. Each delta references a fixed
  base independently. Maximum record size: 8,192 bytes.
- `BardBoxRecordChunk.h`: CRC framing and bounded reader. First frame is the raw
  base; at most 49 later frames are encoded records. Wire bytes are documented in
  the canonical storage standard.
- `tests/record_delta.cpp` and `tests/record_chunk.cpp`: corruption and truncation
  tests. Run `python -m pytest tests/test_record_delta.py` with a C++11 compiler
  supporting address and undefined-behavior sanitizers.

## Project integration checklist

1. Copy both headers unchanged. Keep acquisition independent of storage/uploads.
   Allocate codec and cursor objects on the heap; measure peak memory and latency
   on the target while networking is active.
2. Start a chunk with `RecordChunk::header(sequence)` and
   `RecordChunk::frame(rawBase, frame)`. Encode subsequent records with
   `RecordDelta(base).encode(record, encoded)` then frame the result. Check return
   values, the 50-record bound, valid payload sizes and free space before writing.
3. Flush/close data before committing queue metadata. Rescan after an uncertain
   append. A complete header with no records is reusable: do not append a second
   header. Serialize scans and writes using the project's storage lock.
4. Read with `RecordChunk(sequence).next()`, a stable file length and a callback
   `(offset, destination, count) -> bytesRead`. Handle all results: `Record`,
   `End`, `Incomplete`, `Corrupt`, `WrongChunk`, `Full`. Do not interpret an I/O
   error as an empty queue. Cursor offsets cover verified bytes only.
5. Seal damaged tails, retain their bytes, and start a new sequence. Do not guess
   how to resume beyond corruption. Report damage and capacity exhaustion.
6. Upload oldest-first with stable record IDs. Commit ACK metadata atomically
   before deleting consumed files. If commit fails, restore the RAM cursor/count
   too. The receiver must deduplicate replay after uncertain commits. Reclaim
   obsolete files only when durable ACK metadata proves they were consumed.
7. Keep old storage formats readable; do not rewrite queued data in place or
   format automatically on mount failure. Document blank-device commissioning
   and export/recovery. Bind OTA compatibility to queue schema: rollback firmware
   must understand every format that remains on the device.

CESH's LittleFS adapter and 11,000-record cap are project choices. Do not copy its
sensor code or add HTTP to serial-only projects just to inherit these components.

## Capacity and acceptance

Required records = days * 86,400 / interval_seconds, plus operational headroom.
CESH's 14-day target at 120 seconds is 10,080 records. Its production queue passed
exact ordered replay after simulated restart; exported files fit a 4,063,232-byte
LittleFS image and unpacked unchanged. This is not a universal capacity guarantee.

`scripts/measure_record_delta.py` estimates encoded sizes and can save JSONL with
`--records-output`. Its generated CESH workload is an example, not a shared sensor
schema. Supply an actual completed-payload fixture and the compiled codec harness
(`tests/record_delta.cpp`). CESH's `scripts/check_queue_retention.py` additionally
tests its production adapter and filesystem image. Other adapters require their
own equivalent validation against their storage geometry.

Before deployment test full storage, mixed versions, failed writes and ACK commits,
repeated append/delete cycles, power interruption, restart, replay, acquisition
timing and heap use. Damaged retained files, legacy uncompressed data and filesystem
overhead reduce capacity. Record computer, filesystem-image and physical evidence
separately. Compression tests alone do not establish field retention.
