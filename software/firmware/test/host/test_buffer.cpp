// Host tests for the durable BardBoxBuffer with a fault-injecting in-memory
// storage. Built with BARDBOX_BUFFER_CAPACITY_RECORDS=4. Simulated restarts
// only: physical flash durability and power loss are not verified here.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "BardBoxBuffer.h"
#include "BardBoxRecordCodec.h"
#include "FakeStorage.h"

static int failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++failures;                                                                \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                            \
  } while (0)

using bbcore::BardBoxBuffer;
using bbcore::QueueState;
using bbcore::QueueToken;
using bbcore::Record;
typedef BardBoxBuffer::AckResult Ack;

// One float channel whose value, name and unit are set by the test.
struct ScriptedSource : bbcore::SnapshotSource {
  double value = 0;
  uint32_t nowMs = 0;
  uint32_t publishSeq = 0;
  std::string sensor = "bme280", name = "humidity", unit = "%";
  uint8_t snapshotChannels(bbcore::RecordChannel *out, uint32_t &capturedMs) override {
    capturedMs = nowMs;
    out[0] = bbcore::RecordChannel();
    out[0].hasSample = true;
    out[0].publishSeq = ++publishSeq;
    out[0].value.kind = bbcore::ValueKind::Float;
    out[0].value.status = bbcore::ChannelStatus::Valid;
    out[0].value.f = value;
    return 1;
  }
  void describeChannels(bbcore::ChannelSchema *out) override {
    out[0] = bbcore::ChannelSchema();
    strcpy(out[0].sensor, sensor.c_str());
    strcpy(out[0].name, name.c_str());
    strcpy(out[0].unit, unit.c_str());
  }
};

static const uint32_t kInterval = BardBoxBuffer::recordingIntervalMs();

static void begin(BardBoxBuffer &b, FakeStorage &fs, uint32_t t, uint32_t boot) {
  b.setAttribution("bb-tst-air-001", "1.0.0", "v2-candidate");
  b.begin(t, boot, &fs, boot * 2654435761u + 7);
}

static void recordOne(BardBoxBuffer &b, ScriptedSource &src, uint32_t &t, double value) {
  t += kInterval;
  src.nowMs = t;
  src.value = value;
  CHECK(b.service(t, src));
}

static bool oldest(BardBoxBuffer &b, double &value, QueueToken &token, Record *out = nullptr) {
  static Record r;
  if (!b.peekOldest(r, token)) return false;
  value = r.channels[0].value.f;
  if (out) *out = r;
  return true;
}

static void drain(BardBoxBuffer &b, std::vector<double> &values) {
  double v;
  QueueToken tok;
  while (oldest(b, v, tok)) {
    values.push_back(v);
    CHECK(b.acknowledge(tok) == Ack::Removed);
  }
}

// Writes a valid record file directly, as an earlier firmware would have.
static void writeRecordFile(FakeStorage &fs, uint32_t generation, uint32_t seq, double value) {
  static Record r;
  r = Record();
  r.queueGeneration = generation;
  r.queueSeq = seq;
  r.channelCount = 1;
  r.channels[0].hasSample = true;
  r.channels[0].value.status = bbcore::ChannelStatus::Valid;
  r.channels[0].value.f = value;
  uint8_t frame[bbcore::kMaxFrameBytes];
  const size_t n = bbcore::encodeRecordFrame(r, frame, sizeof frame);
  char p[32];
  snprintf(p, sizeof p, "/bbq/r%010lu", (unsigned long)seq);
  fs.dirs.insert("/bbq");
  fs.files[p].assign(frame, frame + n);
}

// Writes meta directly: "BBM3" (with flags) by default, or the older "BBM2".
static void writeMeta(FakeStorage &fs, uint32_t gen, uint32_t next, uint32_t evicted, uint32_t pending,
                      uint32_t flags = 0, bool v2 = false) {
  uint8_t m[28];
  memcpy(m, v2 ? "BBM2" : "BBM3", 4);
  const uint32_t v[5] = {gen, next, evicted, pending, flags};
  const int fields = v2 ? 4 : 5;
  for (int k = 0; k < fields; ++k)
    for (int i = 0; i < 4; ++i) m[4 + 4 * k + i] = (v[k] >> (8 * i)) & 0xFF;
  const size_t body = 4 + 4 * fields;
  const uint32_t crc = bbcore::crc32(m, body);
  for (int i = 0; i < 4; ++i) m[body + i] = (crc >> (8 * i)) & 0xFF;
  fs.dirs.insert("/bbq");
  fs.files["/bbq/meta"].assign(m, m + body + 4);
}

static uint32_t metaNextSeq(FakeStorage &fs) {
  const std::vector<uint8_t> &m = fs.files["/bbq/meta"];
  return m[8] | (m[9] << 8) | (m[10] << 16) | ((uint32_t)m[11] << 24);
}

static void test_restart_preserves_order_and_exact_ack() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  QueueToken first;
  {
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    CHECK(b.status().durable);
    recordOne(b, src, t, 1.5);
    recordOne(b, src, t, 2.5);
    recordOne(b, src, t, 3.5);
    double v;
    CHECK(oldest(b, v, first) && first.seq == 1 && v == 1.5);
    QueueToken wrong = first;
    wrong.seq = 2;
    CHECK(b.acknowledge(wrong) == Ack::StaleToken);  // not the head
    CHECK(b.acknowledge(first) == Ack::Removed);
    CHECK(b.acknowledge(first) == Ack::StaleToken);  // already gone
  }
  BardBoxBuffer b;
  begin(b, fs, t, 2);
  CHECK(b.status().stored == 2 && b.status().metaRebuilt == 0);
  Record r;
  double v;
  QueueToken tok;
  CHECK(oldest(b, v, tok, &r) && tok.seq == 2 && tok.generation == first.generation && v == 2.5);
  CHECK(!b.latest(r));  // latest completed record is per boot
  recordOne(b, src, t, 4.5);
  std::vector<double> got;
  drain(b, got);
  CHECK((got == std::vector<double>{2.5, 3.5, 4.5}));
}

// Codex repro 1: drain, lose meta, restart -> the old token must not match.
static void test_lost_or_corrupt_meta_after_drain_never_reuses_token() {
  for (int corrupt = 0; corrupt < 2; ++corrupt) {
    FakeStorage fs;
    ScriptedSource src;
    uint32_t t = 0;
    QueueToken old;
    {
      BardBoxBuffer b;
      begin(b, fs, t, 1);
      recordOne(b, src, t, 1);
      double v;
      CHECK(oldest(b, v, old));
      CHECK(b.acknowledge(old) == Ack::Removed);
    }
    if (corrupt) fs.files["/bbq/meta"][5] ^= 0xFF;
    else fs.files.erase("/bbq/meta");
    BardBoxBuffer b;
    begin(b, fs, t, 2);
    CHECK(b.status().metaRebuilt == (corrupt ? 1u : 0u));  // empty + missing meta looks like first use
    recordOne(b, src, t, 2);
    double v;
    QueueToken now;
    CHECK(oldest(b, v, now) && v == 2);
    CHECK(now.generation != old.generation);
    CHECK(b.acknowledge(old) == Ack::StaleToken);  // stale ACK deletes nothing
    CHECK(b.status().stored == 1);
  }
}

static void test_sequence_exhaustion() {
  FakeStorage fs;
  writeMeta(fs, 77, 0xFFFFFFFEu, 0, 0);
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  begin(b, fs, t, 1);
  recordOne(b, src, t, 1);  // seq 0xFFFFFFFE
  recordOne(b, src, t, 2);  // seq 0xFFFFFFFF, then exhausted
  recordOne(b, src, t, 3);  // blocked: queue not empty
  CHECK(b.status().state == QueueState::SequenceExhausted && b.status().pendingRam == 1);
  std::vector<double> got;
  drain(b, got);
  CHECK((got == std::vector<double>{1, 2}));
  b.service(t, src);  // now empty: a new generation starts at sequence 1
  double v;
  QueueToken tok;
  CHECK(oldest(b, v, tok) && v == 3 && tok.seq == 1 && tok.generation == 78);
  CHECK(b.status().state == QueueState::Ok);
}

// Codex repro 2: more saved records than capacity are never hidden.
static void test_over_capacity_recovery_delivers_everything() {
  FakeStorage fs;
  for (uint32_t i = 1; i <= 5; ++i) writeRecordFile(fs, 9, i, i);
  writeMeta(fs, 9, 6, 0, 0);
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  begin(b, fs, t, 1);
  CHECK(b.status().stored == 5 && b.status().hidden == 1 && b.status().state == QueueState::OverCapacity);
  recordOne(b, src, t, 99);  // admission blocked, kept in RAM
  CHECK(b.status().pendingRam == 1 && !fs.files.count("/bbq/r0000000006"));
  std::vector<double> got;
  drain(b, got);
  CHECK((got == std::vector<double>{1, 2, 3, 4, 5}));
  CHECK(b.status().state == QueueState::Ok && b.status().stored == 0);
  b.service(t, src);  // the held record is admitted after the backlog
  got.clear();
  drain(b, got);
  CHECK((got == std::vector<double>{99}));
}

static void test_scan_overflow_without_meta_never_overwrites() {
  FakeStorage fs;
  for (uint32_t i = 1; i <= 11; ++i) writeRecordFile(fs, 9, i, i);  // > 2x capacity
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  begin(b, fs, t, 1);
  CHECK(b.status().stored == 11 && b.status().state == QueueState::OverCapacity);
  std::vector<double> got;
  drain(b, got);
  CHECK(got.size() == 11 && got.front() == 1 && got.back() == 11);
  recordOne(b, src, t, 12);
  double v;
  QueueToken tok;
  CHECK(oldest(b, v, tok) && v == 12 && tok.seq == 12);  // above every earlier identity
}

// Codex finding 3: original attribution and schema survive later changes.
static void test_records_keep_original_attribution_and_schema() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    recordOne(b, src, t, 41);
  }
  BardBoxBuffer b;
  b.setAttribution("bb-new-uid-009", "2.0.0", "v3");
  b.begin(t, 2, &fs, 5);
  src.sensor = "sht45";
  src.name = "rh";
  src.unit = "fraction";
  recordOne(b, src, t, 0.42);
  Record r;
  double v;
  QueueToken tok;
  CHECK(oldest(b, v, tok, &r) && v == 41);
  CHECK(strcmp(r.attribution.uid, "bb-tst-air-001") == 0 && strcmp(r.attribution.firmware, "1.0.0") == 0);
  CHECK(strcmp(r.attribution.protocol, "v2-candidate") == 0);
  CHECK(strcmp(r.schema[0].sensor, "bme280") == 0 && strcmp(r.schema[0].name, "humidity") == 0);
  CHECK(strcmp(r.schema[0].unit, "%") == 0 && !r.channels[0].acquiredKnown);  // unknown stays unknown
  b.acknowledge(tok);
  CHECK(oldest(b, v, tok, &r) && strcmp(r.attribution.uid, "bb-new-uid-009") == 0);
  CHECK(strcmp(r.schema[0].unit, "fraction") == 0);
}

// Codex repro 4 and failure boundaries around eviction.
static void test_eviction_accounting_survives_failures() {
  {  // failed append after eviction, then restart
    FakeStorage fs;
    ScriptedSource src;
    uint32_t t = 0;
    {
      BardBoxBuffer b;
      begin(b, fs, t, 1);
      for (int i = 0; i < 4; ++i) recordOne(b, src, t, i);
      // Writes at capacity: eviction intent meta (1), eviction done meta (2),
      // sequence reservation meta (3), record temp file (4). Fail the record.
      fs.failWriteAt = 4;
      recordOne(b, src, t, 4);
      CHECK(b.status().evicted == 1 && b.status().pendingRam == 1);
    }
    CHECK(!fs.files.count("/bbq/r0000000002"));
    BardBoxBuffer b;
    begin(b, fs, t, 2);
    CHECK(b.status().evicted == 1);  // the irreversible loss stays counted
  }
  {  // power lost after removing the victim, before the final meta write
    FakeStorage fs;
    writeRecordFile(fs, 9, 1, 1);
    writeRecordFile(fs, 9, 3, 3);
    writeMeta(fs, 9, 4, 5, 2);  // intent: evicting seq 2, which is already gone
    BardBoxBuffer b;
    begin(b, fs, 0, 1);
    CHECK(b.status().evicted == 6);
  }
  {  // intent recorded but the victim still exists: nothing was lost
    FakeStorage fs;
    writeRecordFile(fs, 9, 1, 1);
    writeRecordFile(fs, 9, 2, 2);
    writeMeta(fs, 9, 3, 5, 2);
    BardBoxBuffer b;
    begin(b, fs, 0, 1);
    CHECK(b.status().evicted == 5 && b.status().stored == 2);
  }
  {  // intent cannot be written: no removal, no count
    FakeStorage fs;
    ScriptedSource src;
    uint32_t t = 0;
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    for (int i = 0; i < 4; ++i) recordOne(b, src, t, i);
    fs.failWriteAt = 1;  // the intent meta write
    recordOne(b, src, t, 4);
    CHECK(b.status().evicted == 0 && b.status().stored == 4 && fs.files.count("/bbq/r0000000002"));
  }
  {  // victim cannot be removed: kept, not counted
    FakeStorage fs;
    ScriptedSource src;
    uint32_t t = 0;
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    for (int i = 0; i < 4; ++i) recordOne(b, src, t, i);
    fs.failRemoves = 1;
    recordOne(b, src, t, 4);
    CHECK(b.status().evicted == 0 && fs.files.count("/bbq/r0000000002"));
  }
}

static void test_failures_retain_records() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  begin(b, fs, t, 1);
  fs.failWriteAt = 2;  // the record temp write (after the reservation)
  recordOne(b, src, t, 10);
  CHECK(b.status().writeFailures == 1 && b.status().pendingRam == 1 && b.status().stored == 0);
  fs.failRenames = 1;  // the reservation meta rename: admission blocked, kept in RAM
  recordOne(b, src, t, 11);
  CHECK(b.status().pendingRam == 2);
  for (int i = 0; i < 4; ++i) b.service(t, src);  // retries oldest first
  CHECK(b.status().pendingRam == 0 && b.status().stored == 2);
  double v;
  QueueToken tok;
  CHECK(oldest(b, v, tok) && v == 10);
  fs.failRemoves = 1;  // ACK removal fails: the record stays the head
  CHECK(b.acknowledge(tok) == Ack::StorageError);
  CHECK(oldest(b, v, tok) && v == 10);
}

static void test_unavailable_storage_is_never_formatted() {
  FakeStorage fs;
  fs.mountFails = true;
  fs.files["/bbq/r0000000007"] = {1, 2, 3};
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  begin(b, fs, t, 1);
  CHECK(!b.status().durable && strcmp(b.status().storage, "unavailable") == 0);
  for (int i = 0; i < BARDBOX_BUFFER_RAM_RECORDS + 2; ++i) recordOne(b, src, t, i);
  CHECK(b.status().pendingRam == BARDBOX_BUFFER_RAM_RECORDS && b.status().lostRam == 2);
  Record r;
  QueueToken tok;
  CHECK(!b.peekOldest(r, tok));
  CHECK(fs.files.size() == 1 && !fs.formatted);
}

static void test_recovery_interrupted_corrupt_and_failed_quarantine() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    for (int i = 1; i <= 4; ++i) recordOne(b, src, t, i);
  }
  fs.files["/bbq/r.tmp"] = {9, 9};
  fs.files["/bbq/r0000000002"].resize(5);   // torn: quarantined
  fs.files["/bbq/r0000000003"][20] ^= 0xFF; // corrupt, and its rename will fail
  fs.failRenameOf = "/bbq/r0000000003";
  BardBoxBuffer b;
  begin(b, fs, t, 2);
  CHECK(b.status().interrupted == 1 && !fs.files.count("/bbq/r.tmp"));
  CHECK(b.status().quarantined == 1 && fs.files.count("/bbq/q0000000002"));
  CHECK(b.status().unreadable == 1 && fs.files.count("/bbq/r0000000003"));  // not claimed quarantined
  std::vector<double> got;
  drain(b, got);
  CHECK((got == std::vector<double>{1, 4}));  // unreadable records never delivered
}

static void test_saturation_keeps_anchor() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    for (int i = 1; i <= 6; ++i) recordOne(b, src, t, i);
    CHECK(b.status().stored == 4 && b.status().evicted == 2);
  }
  BardBoxBuffer b;
  begin(b, fs, t, 2);
  CHECK(b.status().evicted == 2);
  std::vector<double> got;
  drain(b, got);
  CHECK((got == std::vector<double>{1, 4, 5, 6}));
}

static void test_frame_is_separate_and_checked() {
  static Record r, back;
  r = Record();
  r.bootId = 7;
  r.seq = 9;
  r.queueGeneration = 3;
  r.queueSeq = 4;
  strcpy(r.attribution.uid, "bb-tst-air-001");
  r.channelCount = 1;
  strcpy(r.schema[0].name, "pm25");
  r.channels[0].value.status = bbcore::ChannelStatus::NonFinite;
  uint8_t frame[bbcore::kMaxFrameBytes];
  const size_t len = bbcore::encodeRecordFrame(r, frame, sizeof frame);
  CHECK(len > 11 && memcmp(frame, "BBQ1", 4) == 0 && frame[11] == bbcore::kPayloadSchemaCandidate);
  CHECK(bbcore::decodeRecordFrame(frame, len, back) == bbcore::DecodeResult::Ok);
  CHECK(back.queueSeq == 4 && strcmp(back.schema[0].name, "pm25") == 0);
  frame[len - 1] ^= 1;
  CHECK(bbcore::decodeRecordFrame(frame, len, back) == bbcore::DecodeResult::BadChecksum);
  CHECK(bbcore::decodeRecordFrame(frame, len - 1, back) == bbcore::DecodeResult::BadFrame);
}


// Re-review repro: a valid exhausted meta (next 0) must stay exhausted.
static void test_exhausted_meta_survives_restart() {
  {  // empty queue: roll to a new generation, never back to sequence 1 of 77
    FakeStorage fs;
    writeMeta(fs, 77, 0, 0, 0, 0, true);  // the older BBM2 format is still read
    ScriptedSource src;
    uint32_t t = 0;
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    recordOne(b, src, t, 10);
    double v;
    QueueToken tok;
    CHECK(oldest(b, v, tok) && v == 10 && tok.generation == 78 && tok.seq == 1);
    QueueToken historical;
    historical.generation = 77;
    historical.seq = 1;
    CHECK(b.acknowledge(historical) == Ack::StaleToken && b.status().stored == 1);
  }
  {  // low-sequence backlog: block, deliver the backlog, then roll
    FakeStorage fs;
    writeRecordFile(fs, 77, 1, 1);
    writeRecordFile(fs, 77, 2, 2);
    writeMeta(fs, 77, 0, 0, 0);
    ScriptedSource src;
    uint32_t t = 0;
    {
      BardBoxBuffer b;
      begin(b, fs, t, 1);
      CHECK(b.status().state == QueueState::SequenceExhausted && b.status().stored == 2);
      recordOne(b, src, t, 3);
      CHECK(b.status().pendingRam == 1 && b.status().stored == 2);
    }
    BardBoxBuffer b;  // restart while still exhausted: still blocked
    begin(b, fs, t, 2);
    CHECK(b.status().state == QueueState::SequenceExhausted);
    std::vector<double> got;
    drain(b, got);
    CHECK((got == std::vector<double>{1, 2}));
    recordOne(b, src, t, 4);  // empty now: generation 78 starts
    double v;
    QueueToken tok;
    CHECK(oldest(b, v, tok) && v == 4 && tok.generation == 78 && tok.seq == 1);
  }
}

static void test_repeated_rollovers_across_restarts_keep_order() {
  FakeStorage fs;
  writeMeta(fs, 77, 0xFFFFFFFFu, 0, 0);
  ScriptedSource src;
  uint32_t t = 0;
  std::vector<double> got;
  for (uint32_t round = 0; round < 2; ++round) {
    {
      BardBoxBuffer b;
      begin(b, fs, t, 10 + round);
      recordOne(b, src, t, 100 + round);  // last sequence of this generation
      recordOne(b, src, t, 200 + round);  // exhausted with a backlog: held in RAM
      CHECK(b.status().state == QueueState::SequenceExhausted);
      CHECK(metaNextSeq(fs) == 0);
    }  // restart; the RAM-held record is lost with the boot (counted as such)
    BardBoxBuffer b;
    begin(b, fs, t, 20 + round);
    CHECK(b.status().state == QueueState::SequenceExhausted && b.status().generation == 77 + round);
    drain(b, got);
    recordOne(b, src, t, 300 + round);  // empty: roll to the next generation
    double v;
    QueueToken tok;
    CHECK(oldest(b, v, tok) && tok.generation == 78 + round && tok.seq == 1);
    drain(b, got);
    // Push the new generation to its last sequence for the next round.
    writeMeta(fs, 78 + round, 0xFFFFFFFFu, b.status().evicted, 0);
  }
  CHECK((got == std::vector<double>{100, 300, 101, 301}));  // FIFO within and across generations
}

static void test_attribution_is_validated_never_truncated() {
  const std::string u32(32, 'u'), u33(33, 'u'), p16(16, 'p'), p17(17, 'p');
  BardBoxBuffer a;
  CHECK(a.setAttribution(u32.c_str(), u32.c_str(), p16.c_str()));  // longest supported
  BardBoxBuffer b1, b2, b3, b4, b5;
  CHECK(!b1.setAttribution(u33.c_str(), "1.0.0", "v2"));
  CHECK(!b2.setAttribution("bb-tst-air-001", u33.c_str(), "v2"));
  CHECK(!b3.setAttribution("bb-tst-air-001", "1.0.0", p17.c_str()));
  CHECK(!b4.setAttribution("", "1.0.0", "v2"));
  CHECK(!b5.setAttribution("bb-tst-air-001", "", "v2"));
  FakeStorage fs;
  ScriptedSource src;
  CHECK(!b1.begin(0, 1, &fs, 5));  // no valid attribution: nothing starts or records
  src.nowMs = BardBoxBuffer::recordingIntervalMs();
  CHECK(!b1.service(src.nowMs, src) && fs.files.empty());
  CHECK(a.begin(0, 1, &fs, 5));
  CHECK(!a.setAttribution("bb-other-uid-01", "1.0.0", "v2"));  // fixed once started
  CHECK(a.service(src.nowMs, src));
  Record r;
  QueueToken tok;
  CHECK(a.peekOldest(r, tok) && std::string(r.attribution.uid) == u32 && std::string(r.attribution.protocol) == p16);
}

static void test_loss_history_unknown_after_meta_loss_persists() {
  FakeStorage fs;
  writeRecordFile(fs, 9, 1, 1);  // records but no meta: history unknown
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    begin(b, fs, t, 1);
    CHECK(!b.status().lossHistoryKnown && b.status().metaRebuilt == 1 && b.status().evicted == 0);
  }
  BardBoxBuffer b;
  begin(b, fs, t, 2);
  CHECK(!b.status().lossHistoryKnown && b.status().metaRebuilt == 0);  // still unknown, not proven zero
  FakeStorage clean;
  writeMeta(clean, 5, 1, 3, 0);
  BardBoxBuffer c;
  begin(c, clean, 0, 3);
  CHECK(c.status().lossHistoryKnown && c.status().evicted == 3);
}

int main() {
  test_restart_preserves_order_and_exact_ack();
  test_lost_or_corrupt_meta_after_drain_never_reuses_token();
  test_sequence_exhaustion();
  test_over_capacity_recovery_delivers_everything();
  test_scan_overflow_without_meta_never_overwrites();
  test_records_keep_original_attribution_and_schema();
  test_eviction_accounting_survives_failures();
  test_failures_retain_records();
  test_unavailable_storage_is_never_formatted();
  test_recovery_interrupted_corrupt_and_failed_quarantine();
  test_saturation_keeps_anchor();
  test_frame_is_separate_and_checked();
  test_exhausted_meta_survives_restart();
  test_repeated_rollovers_across_restarts_keep_order();
  test_attribution_is_validated_never_truncated();
  test_loss_history_unknown_after_meta_loss_persists();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox buffer tests: all checks passed (simulated storage and restarts)\n");
  return 0;
}
