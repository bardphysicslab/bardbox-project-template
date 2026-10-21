#pragma once

#include <stdint.h>

#include "BardBoxBufferConfig.h"
#include "BardBoxRecord.h"
#include "BardBoxRecordCodec.h"
#include "BardBoxStorage.h"

namespace bbcore {

// Delivery token: a persisted queue generation plus a sequence within it. A
// lost or corrupt meta file starts a new random generation, so a token from
// before cannot match a record written after.
struct QueueToken {
  uint32_t generation = 0;
  uint32_t seq = 0;
};

// Why admission is blocked. Delivery of existing records continues.
enum class QueueState : uint8_t {
  Ok,
  OverCapacity,       // more saved records than capacity: deliver them first
  SequenceExhausted,  // no sequence left in this generation and queue not empty
  PathConflict,       // a record file already exists where a new one would go
  MetaWriteFailed,    // queue identity could not be persisted
};
const char *queueStateName(QueueState state);

struct BufferStatus {
  const char *storage = "none";  // adapter name, or "unavailable"
  bool durable = false;
  QueueState state = QueueState::Ok;
  uint32_t generation = 0;
  uint32_t stored = 0;           // persisted records awaiting acknowledgement
  uint32_t hidden = 0;           // of those, beyond the in-memory index (over capacity)
  uint32_t capacity = BARDBOX_BUFFER_CAPACITY_RECORDS;
  uint32_t evicted = 0;          // saturation loss (persisted, recoverable)
  uint32_t pendingRam = 0;
  uint32_t lostRam = 0;          // dropped because the RAM retry queue was full
  uint32_t writeFailures = 0;
  uint32_t interrupted = 0;      // unfinished writes removed during recovery
  uint32_t quarantined = 0;      // unreadable records moved aside (rename succeeded)
  uint32_t unreadable = 0;       // unreadable records that could not be moved aside
  uint32_t metaRebuilt = 0;      // meta was missing or corrupt: new generation started
  bool lossHistoryKnown = true;  // false: evicted counts only since meta was lost
  uint32_t skippedBoundaries = 0;
};

// Owns the recording interval, boundaries, record creation from a cache
// snapshot, durable admission, FIFO delivery order and acknowledgement.
//
// Storage: one immutable file per record (/bbq/r<seq>, written to a temp file
// and renamed, never overwritten); /bbq/meta holds the queue generation, next
// sequence, eviction count and any eviction in progress. Each record carries
// its original attribution and channel schema. Recovery never formats.
class BardBoxBuffer {
 public:
  enum class AckResult : uint8_t { Removed, StaleToken, Empty, StorageError };

  // Attribution stamped on every new record. Required before begin. Returns
  // false (and keeps nothing) for an empty UID or firmware, or a value longer
  // than the stored field (UID and firmware 32, protocol 16 characters):
  // values are never truncated.
  bool setAttribution(const char *uid, const char *firmware, const char *protocol);
  // storage may be null (RAM only). generationSeed should be random per boot
  // (it seeds a new queue generation when meta must be rebuilt). Returns false,
  // recording nothing, if no valid attribution was set.
  bool begin(uint32_t nowMs, uint32_t bootId, StorageAdapter *storage, uint32_t generationSeed);
  bool started() const { return started_; }
  // UTC for record creation. With timestamps off, records are marked
  // Disabled and the source is not asked; with no source they are Unsynced.
  void setUtcSource(UtcSource *utc, bool recordTimestamps) {
    utc_ = utc;
    recordTimestamps_ = recordTimestamps;
  }

  static constexpr uint32_t recordingIntervalMs() {
    return static_cast<uint32_t>(BARDBOX_RECORDING_INTERVAL_S) * 1000UL;
  }

  // Bounded step: retries a blocked meta write or one RAM-held record, then
  // creates at most one record when a boundary is due.
  bool service(uint32_t nowMs, SnapshotSource &source);

  bool peekOldest(Record &out, QueueToken &token);
  AckResult acknowledge(const QueueToken &token);

  bool latest(Record &out) const;
  BufferStatus status() const;

 private:
  struct Entry {
    uint32_t generation;
    uint32_t seq;
  };
  bool recover();
  bool refillIndex();
  bool persist(const Record &record);
  bool evictOldestUnprotected();
  bool writeMeta();
  bool admissionAllowed();
  Entry indexAt(uint32_t position) const;
  void indexPush(const Entry &entry);
  void indexPopFront();
  void indexRemoveAt(uint32_t position);
  void ramPush(const Record &record);
  static void listEntry(const char *name, void *context);

  bool started_ = false;
  bool durable_ = false;
  StorageAdapter *storage_ = nullptr;
  UtcSource *utc_ = nullptr;
  bool recordTimestamps_ = true;
  uint32_t bootId_ = 0;
  uint32_t nextDueMs_ = 0;
  uint32_t recordSeq_ = 0;
  RecordAttribution attribution_;
  bool attributionSet_ = false;

  // Persisted in meta.
  uint32_t generation_ = 0;
  uint32_t nextSeq_ = 1;
  uint32_t pendingVictim_ = 0;  // seq being evicted (0 = none)
  uint32_t generationSeed_ = 1;

  bool hasLatest_ = false;
  Record latest_;
  Record scratch_;
  uint8_t frame_[kMaxFrameBytes];
  BufferStatus status_;

  Entry index_[BARDBOX_BUFFER_CAPACITY_RECORDS] = {};  // oldest first
  uint32_t indexHead_ = 0;
  uint32_t indexCount_ = 0;
  uint32_t onDisk_ = 0;  // valid record files, including those beyond the index
  uint32_t lastIndexedSeq_ = 0;

  Record ram_[BARDBOX_BUFFER_RAM_RECORDS];
  uint8_t ramHead_ = 0;
  uint8_t ramCount_ = 0;
};

}  // namespace bbcore
