#include "BardBoxBuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BardBoxRecordCodec.h"

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

namespace {

constexpr const char *kDir = "/bbq";
constexpr const char *kTmp = "/bbq/r.tmp";
constexpr const char *kMeta = "/bbq/meta";
constexpr const char *kMetaTmp = "/bbq/meta.tmp";

bool reached(uint32_t nowMs, uint32_t dueMs) {
  return static_cast<int32_t>(nowMs - dueMs) >= 0;  // wraparound-safe
}

void recordPath(char *out, size_t cap, uint32_t seq) { snprintf(out, cap, "/bbq/r%010lu", static_cast<unsigned long>(seq)); }
void quarantinePath(char *out, size_t cap, uint32_t seq) { snprintf(out, cap, "/bbq/q%010lu", static_cast<unsigned long>(seq)); }

// Parses "r0000000042" into 42. Rejects anything else.
bool parseRecordName(const char *name, uint32_t &seq) {
  if (name[0] != 'r' || strlen(name) != 11) return false;
  for (int i = 1; i < 11; ++i)
    if (name[i] < '0' || name[i] > '9') return false;
  seq = static_cast<uint32_t>(strtoul(name + 1, nullptr, 10));
  return seq != 0;
}

struct Scan {
  uint32_t seqs[BARDBOX_BUFFER_CAPACITY_RECORDS * 2] = {};
  uint32_t count = 0;
  bool overflow = false;
  bool hadTmp = false;
};

}  // namespace

void BardBoxBuffer::listRecord(const char *name, void *context) {
  Scan *scan = static_cast<Scan *>(context);
  uint32_t seq = 0;
  if (strstr(name, ".tmp")) {
    scan->hadTmp = true;
  } else if (parseRecordName(name, seq)) {
    if (scan->count < sizeof scan->seqs / sizeof scan->seqs[0]) scan->seqs[scan->count++] = seq;
    else scan->overflow = true;
  }
}

void BardBoxBuffer::begin(uint32_t nowMs, uint32_t bootId, StorageAdapter *storage) {
  bootId_ = bootId;
  nextDueMs_ = nowMs + recordingIntervalMs();
  storage_ = storage;
  status_ = BufferStatus();
  if (storage_) {
    status_.storage = storage_->name();
    durable_ = storage_->mount() && recover();
    if (!durable_) status_.storage = "unavailable";  // never formatted
  }
  status_.durable = durable_;
  started_ = true;
}

bool BardBoxBuffer::recover() {
  if (!storage_->exists(kDir) && !storage_->makeDir(kDir)) return false;
  static Scan scan;  // large; kept out of the stack
  scan = Scan();
  if (!storage_->list(kDir, &BardBoxBuffer::listRecord, &scan)) return false;
  if (scan.hadTmp) {  // a write was interrupted before its rename
    if (storage_->exists(kTmp) && storage_->remove(kTmp)) ++status_.interrupted;
    if (storage_->exists(kMetaTmp)) storage_->remove(kMetaTmp);
  }
  // Sort ascending (small n; insertion sort keeps code simple).
  for (uint32_t i = 1; i < scan.count; ++i) {
    const uint32_t v = scan.seqs[i];
    uint32_t j = i;
    for (; j > 0 && scan.seqs[j - 1] > v; --j) scan.seqs[j] = scan.seqs[j - 1];
    scan.seqs[j] = v;
  }
  uint8_t frame[kMaxFrameBytes];
  uint32_t maxSeq = 0;
  for (uint32_t i = 0; i < scan.count; ++i) {
    const uint32_t seq = scan.seqs[i];
    if (seq > maxSeq) maxSeq = seq;
    char path[24];
    recordPath(path, sizeof path, seq);
    size_t len = 0;
    Record check;
    const bool readable = storage_->readFile(path, frame, sizeof frame, len) &&
                          decodeRecordFrame(frame, len, check) == DecodeResult::Ok;
    if (!readable) {  // set aside, never delivered or silently dropped
      char q[24];
      quarantinePath(q, sizeof q, seq);
      storage_->rename(path, q);
      ++status_.quarantined;
      continue;
    }
    if (indexCount_ < BARDBOX_BUFFER_CAPACITY_RECORDS) indexPush(seq);
    // Records beyond capacity (after a capacity reduction) stay on storage.
  }
  // Meta: next queue sequence and eviction count. The next sequence is never
  // below what storage shows, so a lost meta update cannot reuse a token.
  uint8_t meta[16];
  size_t metaLen = 0;
  uint32_t metaNext = 1;
  if (storage_->readFile(kMeta, meta, sizeof meta, metaLen) && metaLen == 16 &&
      memcmp(meta, "BBM1", 4) == 0) {
    uint32_t next = 0, evicted = 0, crc = 0;
    memcpy(&next, meta + 4, 4);
    memcpy(&evicted, meta + 8, 4);
    memcpy(&crc, meta + 12, 4);
    if (crc32(meta, 12) == crc) {
      metaNext = next;
      status_.evicted = evicted;
    }
  }
  nextQueueSeq_ = metaNext > maxSeq ? metaNext : maxSeq + 1;
  return true;
}

bool BardBoxBuffer::writeMeta() {
  uint8_t meta[16];
  memcpy(meta, "BBM1", 4);
  memcpy(meta + 4, &nextQueueSeq_, 4);
  memcpy(meta + 8, &status_.evicted, 4);
  const uint32_t crc = crc32(meta, 12);
  memcpy(meta + 12, &crc, 4);
  return storage_->writeFile(kMetaTmp, meta, sizeof meta) && storage_->rename(kMetaTmp, kMeta);
}

uint32_t BardBoxBuffer::indexAt(uint32_t position) const {
  return index_[(indexHead_ + position) % BARDBOX_BUFFER_CAPACITY_RECORDS];
}

void BardBoxBuffer::indexPush(uint32_t seq) {
  index_[(indexHead_ + indexCount_) % BARDBOX_BUFFER_CAPACITY_RECORDS] = seq;
  ++indexCount_;
}

void BardBoxBuffer::indexRemoveAt(uint32_t position) {
  for (uint32_t p = position; p + 1 < indexCount_; ++p)
    index_[(indexHead_ + p) % BARDBOX_BUFFER_CAPACITY_RECORDS] =
        index_[(indexHead_ + p + 1) % BARDBOX_BUFFER_CAPACITY_RECORDS];
  --indexCount_;
}

bool BardBoxBuffer::persist(const Record &record) {
  // Saturation: keep the oldest unacknowledged record as the anchor and evict
  // the next oldest to admit the newest. Eviction is counted data loss.
  if (indexCount_ >= BARDBOX_BUFFER_CAPACITY_RECORDS) {
    char victim[24];
    recordPath(victim, sizeof victim, indexAt(1));
    if (!storage_->remove(victim) && storage_->exists(victim)) {
      ++status_.writeFailures;
      return false;
    }
    indexRemoveAt(1);
    ++status_.evicted;
  }
  uint8_t frame[kMaxFrameBytes];
  const size_t len = encodeRecordFrame(record, frame, sizeof frame);
  const uint32_t seq = nextQueueSeq_;
  char path[24];
  recordPath(path, sizeof path, seq);
  bool ok = len > 0 && storage_->writeFile(kTmp, frame, len);
  if (ok && !storage_->rename(kTmp, path)) ok = storage_->exists(path);  // ambiguous: verify
  if (!ok) {
    ++status_.writeFailures;
    if (storage_->exists(kTmp)) storage_->remove(kTmp);
    return false;
  }
  ++nextQueueSeq_;
  indexPush(seq);
  if (!writeMeta()) ++status_.writeFailures;  // recovery still never reuses seq
  return true;
}

void BardBoxBuffer::ramPush(const Record &record) {
  if (ramCount_ >= BARDBOX_BUFFER_RAM_RECORDS) {
    ++status_.lostRam;  // visible loss: newest dropped, older order kept
    return;
  }
  ram_[(ramHead_ + ramCount_) % BARDBOX_BUFFER_RAM_RECORDS] = record;
  ++ramCount_;
}

bool BardBoxBuffer::service(uint32_t nowMs, SnapshotSource &source) {
  if (!started_) return false;
  if (durable_ && ramCount_ > 0 && persist(ram_[ramHead_])) {  // oldest first
    ramHead_ = (ramHead_ + 1) % BARDBOX_BUFFER_RAM_RECORDS;
    --ramCount_;
  }
  if (!reached(nowMs, nextDueMs_)) return false;
  Record record;
  record.bootId = bootId_;
  record.dueMs = nextDueMs_;
  nextDueMs_ += recordingIntervalMs();
  while (reached(nowMs, nextDueMs_)) {  // late: record once, count the misses
    ++status_.skippedBoundaries;
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
  latest_ = record;  // the latest completed record is set before admission
  hasLatest_ = true;
  if (!(durable_ && ramCount_ == 0 && persist(record))) ramPush(record);
  return true;
}

bool BardBoxBuffer::peekOldest(Record &out, uint32_t &token) {
  if (!durable_ || indexCount_ == 0) return false;
  char path[24];
  recordPath(path, sizeof path, indexAt(0));
  uint8_t frame[kMaxFrameBytes];
  size_t len = 0;
  if (!storage_->readFile(path, frame, sizeof frame, len) ||
      decodeRecordFrame(frame, len, out) != DecodeResult::Ok)
    return false;  // retained; reported as a storage problem by the caller
  token = indexAt(0);
  return true;
}

BardBoxBuffer::AckResult BardBoxBuffer::acknowledge(uint32_t token) {
  if (!durable_ || indexCount_ == 0) return AckResult::Empty;
  if (token != indexAt(0)) return AckResult::StaleToken;  // never removes another record
  char path[24];
  recordPath(path, sizeof path, token);
  if (!storage_->remove(path) && storage_->exists(path)) return AckResult::StorageError;  // retained
  indexHead_ = (indexHead_ + 1) % BARDBOX_BUFFER_CAPACITY_RECORDS;
  --indexCount_;
  return AckResult::Removed;
}

bool BardBoxBuffer::latest(Record &out) const {
  if (!hasLatest_) return false;
  out = latest_;
  return true;
}

BufferStatus BardBoxBuffer::status() const {
  BufferStatus s = status_;
  s.durable = durable_;
  s.stored = indexCount_;
  s.pendingRam = ramCount_;
  return s;
}

}  // namespace bbcore
