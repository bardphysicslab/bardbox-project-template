#pragma once

#include <stdint.h>

#include "BardBoxBufferConfig.h"
#include "BardBoxRecord.h"
#include "BardBoxStorage.h"

namespace bbcore {

struct BufferStatus {
  const char *storage = "none";  // adapter name, or "unavailable"
  bool durable = false;          // a mounted storage adapter is in use
  uint32_t stored = 0;           // records persisted and not yet acknowledged
  uint32_t capacity = BARDBOX_BUFFER_CAPACITY_RECORDS;
  uint32_t evicted = 0;          // saturation loss (persisted count)
  uint32_t pendingRam = 0;       // records waiting in RAM for storage
  uint32_t lostRam = 0;          // dropped because the RAM retry queue was full
  uint32_t writeFailures = 0;
  uint32_t interrupted = 0;      // unfinished writes removed during recovery
  uint32_t quarantined = 0;      // unreadable records set aside, never delivered
  uint32_t skippedBoundaries = 0;
};

// Owns the recording interval, boundaries, record creation from a cache
// snapshot, durable admission, FIFO delivery order and acknowledgement.
//
// Storage: one immutable file per record (/bbq/r<seq>), written to a temp
// file and renamed into place; /bbq/meta keeps the next queue sequence and the
// eviction count. Queue sequences are delivery tokens and never reused while
// the meta file survives. Recovery never formats.
class BardBoxBuffer {
 public:
  enum class AckResult : uint8_t { Removed, StaleToken, Empty, StorageError };

  // storage may be null (RAM only). A failed mount leaves storage unavailable.
  void begin(uint32_t nowMs, uint32_t bootId, StorageAdapter *storage);
  bool started() const { return started_; }

  static constexpr uint32_t recordingIntervalMs() {
    return static_cast<uint32_t>(BARDBOX_RECORDING_INTERVAL_S) * 1000UL;
  }

  // Bounded step: retries at most one RAM-held record, then creates at most
  // one record when a boundary is due. Returns true if a record was created.
  bool service(uint32_t nowMs, SnapshotSource &source);

  // Oldest persisted record and its delivery token. Does not remove it.
  bool peekOldest(Record &out, uint32_t &token);
  // Removes the oldest record only if `token` is exactly its token.
  AckResult acknowledge(uint32_t token);

  bool latest(Record &out) const;
  BufferStatus status() const;

 private:
  bool recover();
  bool persist(const Record &record);
  bool writeMeta();
  void indexPush(uint32_t seq);
  void indexRemoveAt(uint32_t position);
  uint32_t indexAt(uint32_t position) const;
  void ramPush(const Record &record);
  static void listRecord(const char *name, void *context);

  bool started_ = false;
  bool durable_ = false;
  StorageAdapter *storage_ = nullptr;
  uint32_t bootId_ = 0;
  uint32_t nextDueMs_ = 0;
  uint32_t recordSeq_ = 0;
  uint32_t nextQueueSeq_ = 1;
  bool hasLatest_ = false;
  Record latest_;
  BufferStatus status_;

  uint32_t index_[BARDBOX_BUFFER_CAPACITY_RECORDS] = {};  // queue sequences, oldest first
  uint32_t indexHead_ = 0;
  uint32_t indexCount_ = 0;

  Record ram_[BARDBOX_BUFFER_RAM_RECORDS];
  uint8_t ramHead_ = 0;
  uint8_t ramCount_ = 0;
};

}  // namespace bbcore
