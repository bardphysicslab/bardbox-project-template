#pragma once

#include <stdint.h>

#include "BardBoxBufferConfig.h"
#include "BardBoxRecord.h"

namespace bbcore {

// Owns the recording interval, recording boundaries, record assembly from a
// cache snapshot, admission and the latest completed record.
//
// M1: the queue is RAM-only and volatile. Persistent storage, FIFO
// acknowledgement, the saturation anchor and restart recovery are the next
// milestone. No freshness is judged: acquisition time is unknown in M1.
class BardBoxBuffer {
 public:
  enum class Admission : uint8_t { Accepted, Backpressure };

  void begin(uint32_t nowMs, uint32_t bootId);
  bool started() const { return started_; }

  static constexpr uint32_t recordingIntervalMs() {
    return static_cast<uint32_t>(BARDBOX_RECORDING_INTERVAL_S) * 1000UL;
  }

  // Bounded step: creates at most one record when a boundary is due.
  // Returns true if a record was created.
  bool service(uint32_t nowMs, SnapshotSource &source);

  bool latest(Record &out) const;
  uint8_t buffered() const { return count_; }
  uint32_t backpressureCount() const { return backpressure_; }
  uint32_t skippedBoundaries() const { return skipped_; }

 private:
  Admission admit(const Record &record);

  bool started_ = false;
  uint32_t bootId_ = 0;
  uint32_t nextDueMs_ = 0;
  uint32_t recordSeq_ = 0;
  uint32_t skipped_ = 0;
  uint32_t backpressure_ = 0;
  bool hasLatest_ = false;
  Record latest_;
  Record queue_[BARDBOX_BUFFER_RAM_RECORDS];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace bbcore
