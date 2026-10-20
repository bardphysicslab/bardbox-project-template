#include "BardBoxBuffer.h"

namespace bbcore {

const char *channelStatusCode(ChannelStatus status) {
  switch (status) {
    case ChannelStatus::Valid: return "valid";
    case ChannelStatus::NoSample: return "no_sample";
    case ChannelStatus::InvalidType: return "invalid_type";
    case ChannelStatus::NonFinite: return "non_finite";
    case ChannelStatus::NotRepresentable: return "not_representable";
  }
  return "unknown";
}

static bool reached(uint32_t nowMs, uint32_t dueMs) {
  return static_cast<int32_t>(nowMs - dueMs) >= 0;  // wraparound-safe
}

void BardBoxBuffer::begin(uint32_t nowMs, uint32_t bootId) {
  bootId_ = bootId;
  nextDueMs_ = nowMs + recordingIntervalMs();
  started_ = true;
}

bool BardBoxBuffer::service(uint32_t nowMs, SnapshotSource &source) {
  if (!started_ || !reached(nowMs, nextDueMs_)) return false;
  Record record;
  record.bootId = bootId_;
  record.dueMs = nextDueMs_;
  nextDueMs_ += recordingIntervalMs();
  while (reached(nowMs, nextDueMs_)) {  // late: record once, count the misses
    ++skipped_;
    nextDueMs_ += recordingIntervalMs();
  }
  record.seq = ++recordSeq_;
  record.channelCount = source.snapshotChannels(record.channels, record.capturedMs);
  for (uint8_t c = 0; c < record.channelCount; ++c) {
    RecordChannel &ch = record.channels[c];
    ch.repeatedHandoff = hasLatest_ && ch.hasSample && c < latest_.channelCount &&
                         latest_.channels[c].hasSample &&
                         latest_.channels[c].publishSeq == ch.publishSeq;
  }
  admit(record);
  return true;
}

BardBoxBuffer::Admission BardBoxBuffer::admit(const Record &record) {
  latest_ = record;  // the latest completed record is set before admission
  hasLatest_ = true;
  if (count_ >= BARDBOX_BUFFER_RAM_RECORDS) {
    ++backpressure_;
    return Admission::Backpressure;
  }
  queue_[(head_ + count_) % BARDBOX_BUFFER_RAM_RECORDS] = record;
  ++count_;
  return Admission::Accepted;
}

bool BardBoxBuffer::latest(Record &out) const {
  if (!hasLatest_) return false;
  out = latest_;
  return true;
}

}  // namespace bbcore
