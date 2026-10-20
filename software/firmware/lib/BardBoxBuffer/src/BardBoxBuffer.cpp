#include "BardBoxBuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

const char *queueStateName(QueueState state) {
  switch (state) {
    case QueueState::Ok: return "ok";
    case QueueState::OverCapacity: return "over_capacity";
    case QueueState::SequenceExhausted: return "sequence_exhausted";
    case QueueState::PathConflict: return "path_conflict";
    case QueueState::MetaWriteFailed: return "meta_write_failed";
  }
  return "unknown";
}

namespace {

constexpr const char *kDir = "/bbq";
constexpr const char *kTmp = "/bbq/r.tmp";
constexpr const char *kMeta = "/bbq/meta";
constexpr const char *kMetaTmp = "/bbq/meta.tmp";
constexpr size_t kMetaBytes = 24;  // "BBM2" gen next evicted pendingVictim crc

bool reached(uint32_t nowMs, uint32_t dueMs) {
  return static_cast<int32_t>(nowMs - dueMs) >= 0;  // wraparound-safe
}

void recordPath(char *out, size_t cap, uint32_t seq) { snprintf(out, cap, "/bbq/r%010lu", static_cast<unsigned long>(seq)); }
void quarantinePath(char *out, size_t cap, uint32_t seq) { snprintf(out, cap, "/bbq/q%010lu", static_cast<unsigned long>(seq)); }

// Parses "r0000000042" or "q0000000042". Returns the prefix, or 0.
char parseName(const char *name, uint32_t &seq) {
  if ((name[0] != 'r' && name[0] != 'q') || strlen(name) != 11) return 0;
  for (int i = 1; i < 11; ++i)
    if (name[i] < '0' || name[i] > '9') return 0;
  const unsigned long v = strtoul(name + 1, nullptr, 10);
  if (v == 0 || v > 0xFFFFFFFFUL) return 0;
  seq = static_cast<uint32_t>(v);
  return name[0];
}

void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }
uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i); return v; }

// Bounded directory scan: keeps the smallest `limit` record sequences above
// `after`, counts all of them, and tracks the largest sequence of any record
// or quarantined file, so a full scan never misses an existing identity.
struct Scan {
  uint32_t after = 0;
  uint32_t seqs[BARDBOX_BUFFER_CAPACITY_RECORDS] = {};
  uint32_t kept = 0;
  uint32_t total = 0;
  uint32_t maxSeq = 0;
  bool recordTmp = false;
  bool metaTmp = false;
};

void scanAdd(Scan &s, uint32_t seq) {
  ++s.total;
  uint32_t pos = s.kept;
  while (pos > 0 && s.seqs[pos - 1] > seq) --pos;
  if (pos >= BARDBOX_BUFFER_CAPACITY_RECORDS) return;  // not among the smallest
  const uint32_t last = s.kept < BARDBOX_BUFFER_CAPACITY_RECORDS ? s.kept : s.kept - 1;
  for (uint32_t i = last; i > pos; --i) s.seqs[i] = s.seqs[i - 1];
  s.seqs[pos] = seq;
  if (s.kept < BARDBOX_BUFFER_CAPACITY_RECORDS) ++s.kept;
}

Scan scan;  // large; used only from the service task

}  // namespace

void BardBoxBuffer::listEntry(const char *name, void *context) {
  Scan *s = static_cast<Scan *>(context);
  if (strcmp(name, "r.tmp") == 0) { s->recordTmp = true; return; }
  if (strcmp(name, "meta.tmp") == 0) { s->metaTmp = true; return; }
  uint32_t seq = 0;
  const char kind = parseName(name, seq);
  if (!kind) return;
  if (seq > s->maxSeq) s->maxSeq = seq;
  if (kind == 'r' && seq > s->after) scanAdd(*s, seq);
}

static void copyText(char *dst, size_t cap, const char *src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

void BardBoxBuffer::setAttribution(const char *uid, const char *firmware, const char *protocol) {
  copyText(attribution_.uid, sizeof attribution_.uid, uid);
  copyText(attribution_.firmware, sizeof attribution_.firmware, firmware);
  copyText(attribution_.protocol, sizeof attribution_.protocol, protocol);
}

void BardBoxBuffer::begin(uint32_t nowMs, uint32_t bootId, StorageAdapter *storage, uint32_t generationSeed) {
  bootId_ = bootId;
  nextDueMs_ = nowMs + recordingIntervalMs();
  storage_ = storage;
  status_ = BufferStatus();
  generationSeed_ = generationSeed ? generationSeed : 1;
  generation_ = generationSeed_;
  if (storage_) {
    status_.storage = storage_->name();
    durable_ = storage_->mount() && recover();
    if (!durable_) status_.storage = "unavailable";  // never formatted
  }
  started_ = true;
}

bool BardBoxBuffer::writeMeta() {
  uint8_t meta[kMetaBytes];
  memcpy(meta, "BBM2", 4);
  put32(meta + 4, generation_);
  put32(meta + 8, nextSeq_);
  put32(meta + 12, status_.evicted);
  put32(meta + 16, pendingVictim_);
  put32(meta + 20, crc32(meta, 20));
  return storage_->writeFile(kMetaTmp, meta, sizeof meta) && storage_->rename(kMetaTmp, kMeta);
}

bool BardBoxBuffer::recover() {
  if (!storage_->exists(kDir) && !storage_->makeDir(kDir)) return false;

  // 1. Queue identity and accounting from meta, if valid.
  uint8_t meta[kMetaBytes];
  size_t len = 0;
  uint32_t metaNext = 1;
  const bool metaPresent = storage_->exists(kMeta);
  bool metaValid = storage_->readFile(kMeta, meta, sizeof meta, len) && len == kMetaBytes &&
                   memcmp(meta, "BBM2", 4) == 0 && crc32(meta, 20) == get32(meta + 20);
  if (metaValid) {
    generation_ = get32(meta + 4);
    metaNext = get32(meta + 8);
    status_.evicted = get32(meta + 12);
    pendingVictim_ = get32(meta + 16);
    metaValid = generation_ != 0;
  }
  if (!metaValid) {
    // Unknown history: start a new random generation so no earlier token can
    // match a new record. A corrupt meta, or a missing one beside saved
    // records, is reported (the previous eviction count is unknown); a missing
    // meta in an empty queue is indistinguishable from first use.
    status_.metaRebuilt = metaPresent ? 1 : 0;
    generation_ = generationSeed_;
    metaNext = 1;
    status_.evicted = 0;
    pendingVictim_ = 0;
  }

  // 2. Finish an eviction that was in progress: count it only if it happened.
  if (pendingVictim_) {
    char victim[24];
    recordPath(victim, sizeof victim, pendingVictim_);
    if (!storage_->exists(victim)) ++status_.evicted;
    pendingVictim_ = 0;
  }

  // 3. Scan: interrupted writes, record identities and the largest sequence.
  scan = Scan();
  if (!storage_->list(kDir, &BardBoxBuffer::listEntry, &scan)) return false;
  if (scan.recordTmp && storage_->remove(kTmp)) ++status_.interrupted;
  if (scan.metaTmp) storage_->remove(kMetaTmp);
  if (!metaValid && !metaPresent && scan.maxSeq > 0) status_.metaRebuilt = 1;

  // 4. Next sequence: never below meta, never at or below any existing file.
  if (scan.maxSeq == 0xFFFFFFFFUL) {
    nextSeq_ = 0;  // exhausted in this generation
  } else {
    nextSeq_ = metaNext > scan.maxSeq ? metaNext : scan.maxSeq + 1;
  }

  // 5. Index the oldest records (validated); the rest stay counted and are
  // delivered later. Nothing is silently hidden.
  indexHead_ = indexCount_ = 0;
  lastIndexedSeq_ = 0;
  onDisk_ = scan.total;
  for (uint32_t i = 0; i < scan.kept; ++i) {
    const uint32_t seq = scan.seqs[i];
    char path[24];
    recordPath(path, sizeof path, seq);
    size_t n = 0;
    const bool ok = storage_->readFile(path, frame_, sizeof frame_, n) &&
                    decodeRecordFrame(frame_, n, scratch_) == DecodeResult::Ok && scratch_.queueSeq == seq;
    lastIndexedSeq_ = seq;
    if (!ok) {
      char q[24];
      quarantinePath(q, sizeof q, seq);
      if (storage_->rename(path, q)) ++status_.quarantined;
      else ++status_.unreadable;  // left in place, never delivered
      --onDisk_;
      continue;
    }
    Entry e = {scratch_.queueGeneration, seq};
    indexPush(e);
    if (!metaValid && e.generation == generation_) ++generation_;  // avoid a surviving generation
  }
  if (generation_ == 0) generation_ = 1;

  // 6. Persist the recovered identity before admitting anything.
  status_.state = QueueState::Ok;
  if (!writeMeta()) status_.state = QueueState::MetaWriteFailed;
  else if (onDisk_ > indexCount_) status_.state = QueueState::OverCapacity;
  else if (nextSeq_ == 0 && onDisk_ > 0) status_.state = QueueState::SequenceExhausted;
  return true;
}

bool BardBoxBuffer::refillIndex() {
  scan = Scan();
  scan.after = lastIndexedSeq_;
  if (!storage_->list(kDir, &BardBoxBuffer::listEntry, &scan)) return false;
  onDisk_ = indexCount_ + scan.total;
  for (uint32_t i = 0; i < scan.kept && indexCount_ < BARDBOX_BUFFER_CAPACITY_RECORDS; ++i) {
    const uint32_t seq = scan.seqs[i];
    char path[24];
    recordPath(path, sizeof path, seq);
    size_t n = 0;
    const bool ok = storage_->readFile(path, frame_, sizeof frame_, n) &&
                    decodeRecordFrame(frame_, n, scratch_) == DecodeResult::Ok && scratch_.queueSeq == seq;
    lastIndexedSeq_ = seq;
    if (!ok) {
      char q[24];
      quarantinePath(q, sizeof q, seq);
      if (storage_->rename(path, q)) ++status_.quarantined;
      else ++status_.unreadable;
      --onDisk_;
      continue;
    }
    Entry e = {scratch_.queueGeneration, seq};
    indexPush(e);
  }
  if (status_.state == QueueState::OverCapacity && onDisk_ <= indexCount_) status_.state = QueueState::Ok;
  return true;
}

BardBoxBuffer::Entry BardBoxBuffer::indexAt(uint32_t position) const {
  return index_[(indexHead_ + position) % BARDBOX_BUFFER_CAPACITY_RECORDS];
}

void BardBoxBuffer::indexPush(const Entry &entry) {
  index_[(indexHead_ + indexCount_) % BARDBOX_BUFFER_CAPACITY_RECORDS] = entry;
  ++indexCount_;
}

void BardBoxBuffer::indexPopFront() {
  indexHead_ = (indexHead_ + 1) % BARDBOX_BUFFER_CAPACITY_RECORDS;
  --indexCount_;
}

void BardBoxBuffer::indexRemoveAt(uint32_t position) {
  for (uint32_t p = position; p + 1 < indexCount_; ++p)
    index_[(indexHead_ + p) % BARDBOX_BUFFER_CAPACITY_RECORDS] =
        index_[(indexHead_ + p + 1) % BARDBOX_BUFFER_CAPACITY_RECORDS];
  --indexCount_;
}

bool BardBoxBuffer::admissionAllowed() {
  if (!durable_) return false;
  if (status_.state == QueueState::MetaWriteFailed) {
    if (!writeMeta()) return false;
    status_.state = onDisk_ > indexCount_ ? QueueState::OverCapacity : QueueState::Ok;
  }
  if (nextSeq_ == 0) {  // sequence exhausted: start a new generation only when empty
    if (onDisk_ > 0) {
      status_.state = QueueState::SequenceExhausted;
      return false;
    }
    ++generation_;
    if (generation_ == 0) generation_ = 1;
    nextSeq_ = 1;
    lastIndexedSeq_ = 0;
    if (!writeMeta()) {
      status_.state = QueueState::MetaWriteFailed;
      return false;
    }
    status_.state = QueueState::Ok;
  }
  return status_.state == QueueState::Ok;
}

bool BardBoxBuffer::evictOldestUnprotected() {
  // Keep the oldest unacknowledged record as the anchor; evict the next
  // oldest. The intent is persisted first, so the loss count is recoverable.
  const Entry victim = indexAt(1);
  pendingVictim_ = victim.seq;
  if (!writeMeta()) {
    pendingVictim_ = 0;
    ++status_.writeFailures;
    return false;
  }
  char path[24];
  recordPath(path, sizeof path, victim.seq);
  if (!storage_->remove(path) && storage_->exists(path)) {
    pendingVictim_ = 0;
    writeMeta();  // if this fails, recovery sees the file and counts nothing
    ++status_.writeFailures;
    return false;
  }
  indexRemoveAt(1);
  --onDisk_;
  ++status_.evicted;
  pendingVictim_ = 0;
  if (!writeMeta()) {  // recovery still counts it: meta holds the intent
    status_.state = QueueState::MetaWriteFailed;
    ++status_.writeFailures;
    return false;
  }
  return true;
}

bool BardBoxBuffer::persist(const Record &record) {
  if (!admissionAllowed()) return false;
  if (indexCount_ >= BARDBOX_BUFFER_CAPACITY_RECORDS && !evictOldestUnprotected()) return false;

  const uint32_t seq = nextSeq_;
  char path[24];
  recordPath(path, sizeof path, seq);
  if (storage_->exists(path)) {  // never overwrite
    status_.state = QueueState::PathConflict;
    return false;
  }
  // Reserve the sequence durably before writing, so it is never reused.
  nextSeq_ = seq == 0xFFFFFFFFUL ? 0 : seq + 1;
  if (!writeMeta()) {
    nextSeq_ = seq;
    status_.state = QueueState::MetaWriteFailed;
    ++status_.writeFailures;
    return false;
  }
  scratch_ = record;
  scratch_.queueGeneration = generation_;
  scratch_.queueSeq = seq;
  const size_t len = encodeRecordFrame(scratch_, frame_, sizeof frame_);
  bool ok = len > 0 && storage_->writeFile(kTmp, frame_, len);
  if (ok && !storage_->rename(kTmp, path)) ok = storage_->exists(path);  // ambiguous: verify
  if (!ok) {
    ++status_.writeFailures;  // the reserved sequence is skipped, never reused
    if (storage_->exists(kTmp)) storage_->remove(kTmp);
    return false;
  }
  Entry e = {generation_, seq};
  indexPush(e);
  ++onDisk_;
  lastIndexedSeq_ = seq;
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
  uint32_t previousSeq[kMaxChannels] = {};
  bool previousSample[kMaxChannels] = {};
  const uint8_t previousCount = hasLatest_ ? latest_.channelCount : 0;
  for (uint8_t c = 0; c < previousCount; ++c) {
    previousSeq[c] = latest_.channels[c].publishSeq;
    previousSample[c] = latest_.channels[c].hasSample;
  }
  Record &record = latest_;  // the latest completed record is set before admission
  record = Record();
  record.bootId = bootId_;
  record.dueMs = nextDueMs_;
  nextDueMs_ += recordingIntervalMs();
  while (reached(nowMs, nextDueMs_)) {  // late: record once, count the misses
    ++status_.skippedBoundaries;
    nextDueMs_ += recordingIntervalMs();
  }
  record.seq = ++recordSeq_;
  record.attribution = attribution_;
  record.channelCount = source.snapshotChannels(record.channels, record.capturedMs);
  source.describeChannels(record.schema);
  for (uint8_t c = 0; c < record.channelCount; ++c) {
    RecordChannel &ch = record.channels[c];
    ch.repeatedHandoff = c < previousCount && ch.hasSample && previousSample[c] && previousSeq[c] == ch.publishSeq;
  }
  hasLatest_ = true;
  if (!(ramCount_ == 0 && persist(record))) ramPush(record);
  return true;
}

bool BardBoxBuffer::peekOldest(Record &out, QueueToken &token) {
  if (!durable_) return false;
  if (indexCount_ == 0 && onDisk_ > 0) refillIndex();
  if (indexCount_ == 0) return false;
  const Entry head = indexAt(0);
  char path[24];
  recordPath(path, sizeof path, head.seq);
  size_t len = 0;
  if (!storage_->readFile(path, frame_, sizeof frame_, len) ||
      decodeRecordFrame(frame_, len, out) != DecodeResult::Ok || out.queueSeq != head.seq ||
      out.queueGeneration != head.generation)
    return false;  // retained; reported as a storage problem by the caller
  token.generation = head.generation;
  token.seq = head.seq;
  return true;
}

BardBoxBuffer::AckResult BardBoxBuffer::acknowledge(const QueueToken &token) {
  if (!durable_) return AckResult::Empty;
  if (indexCount_ == 0 && onDisk_ > 0) refillIndex();
  if (indexCount_ == 0) return AckResult::Empty;
  const Entry head = indexAt(0);
  if (token.generation != head.generation || token.seq != head.seq) return AckResult::StaleToken;
  char path[24];
  recordPath(path, sizeof path, head.seq);
  if (!storage_->remove(path) && storage_->exists(path)) return AckResult::StorageError;  // retained
  indexPopFront();
  --onDisk_;
  if (indexCount_ == 0 && onDisk_ > 0) refillIndex();
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
  s.generation = generation_;
  s.stored = onDisk_;
  s.hidden = onDisk_ > indexCount_ ? onDisk_ - indexCount_ : 0;
  s.pendingRam = ramCount_;
  return s;
}

}  // namespace bbcore
