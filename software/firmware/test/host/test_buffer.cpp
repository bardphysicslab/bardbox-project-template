// Host tests for the durable BardBoxBuffer with a fault-injecting in-memory
// storage. Built with BARDBOX_BUFFER_CAPACITY_RECORDS=4. Simulated restarts
// only: physical flash durability and power loss are not verified here.
#include <stdio.h>
#include <string.h>

#include <string>

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
using bbcore::Record;

// One float channel whose value is set by the test before each boundary.
struct ScriptedSource : bbcore::SnapshotSource {
  double value = 0;
  uint32_t nowMs = 0;
  uint32_t publishSeq = 0;
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
};

constexpr uint32_t kInterval = BardBoxBuffer::recordingIntervalMs();

// Creates one record per call: advances to the next boundary with `value`.
static void recordOne(BardBoxBuffer &b, ScriptedSource &src, uint32_t &t, double value) {
  t += kInterval;
  src.nowMs = t;
  src.value = value;
  CHECK(b.service(t, src));
}

static bool oldest(BardBoxBuffer &b, double &value, uint32_t &token, uint32_t *bootId = nullptr) {
  Record r;
  if (!b.peekOldest(r, token)) return false;
  value = r.channels[0].value.f;
  if (bootId) *bootId = r.bootId;
  return true;
}

static void test_restart_preserves_order_and_exact_ack() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    b.begin(t, 0xB0071, &fs);
    CHECK(b.status().durable);
    recordOne(b, src, t, 1.5);
    recordOne(b, src, t, 2.5);
    recordOne(b, src, t, 3.5);
    double v; uint32_t tok;
    CHECK(oldest(b, v, tok) && tok == 1 && v == 1.5);
    CHECK(b.acknowledge(2) == BardBoxBuffer::AckResult::StaleToken);  // not the head
    CHECK(b.acknowledge(1) == BardBoxBuffer::AckResult::Removed);
    CHECK(b.acknowledge(1) == BardBoxBuffer::AckResult::StaleToken);  // already gone
    CHECK(b.status().stored == 2);
  }
  {  // restart: same storage, new boot
    BardBoxBuffer b;
    b.begin(t, 0xB0072, &fs);
    CHECK(b.status().stored == 2);
    double v; uint32_t tok, boot;
    CHECK(oldest(b, v, tok, &boot) && tok == 2 && v == 2.5 && boot == 0xB0071);  // original, unchanged
    Record r;
    CHECK(!b.latest(r));  // the latest completed record is per boot
    recordOne(b, src, t, 4.5);
    CHECK(b.acknowledge(2) == BardBoxBuffer::AckResult::Removed);
    CHECK(oldest(b, v, tok) && tok == 3 && v == 3.5);
    CHECK(b.acknowledge(3) == BardBoxBuffer::AckResult::Removed);
    CHECK(oldest(b, v, tok, &boot) && tok == 4 && v == 4.5 && boot == 0xB0072);  // no token reuse
    CHECK(b.acknowledge(4) == BardBoxBuffer::AckResult::Removed);
    CHECK(b.acknowledge(4) == BardBoxBuffer::AckResult::Empty);
  }
  CHECK(!fs.formatted);
}

static void test_failures_retain_records() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  b.begin(t, 1, &fs);
  fs.failWrites = 1;
  recordOne(b, src, t, 10);
  CHECK(b.status().writeFailures == 1 && b.status().pendingRam == 1 && b.status().stored == 0);
  fs.failRenames = 1;  // rename fails: record kept in RAM, temp file cleaned up
  recordOne(b, src, t, 11);
  CHECK(b.status().pendingRam == 2 && !fs.files.count("/bbq/r.tmp"));
  b.service(t, src);  // retries oldest first
  b.service(t, src);
  CHECK(b.status().pendingRam == 0 && b.status().stored == 2);
  double v; uint32_t tok;
  CHECK(oldest(b, v, tok) && v == 10);  // original order kept
  fs.failRemoves = 1;  // ACK removal fails: the record stays the head
  CHECK(b.acknowledge(tok) == BardBoxBuffer::AckResult::StorageError);
  CHECK(oldest(b, v, tok) && v == 10);
}

static void test_unavailable_storage_is_never_formatted() {
  FakeStorage fs;
  fs.mountFails = true;
  fs.files["/bbq/r0000000007"] = {1, 2, 3};  // existing data must stay untouched
  ScriptedSource src;
  uint32_t t = 0;
  BardBoxBuffer b;
  b.begin(t, 1, &fs);
  CHECK(!b.status().durable && strcmp(b.status().storage, "unavailable") == 0);
  for (int i = 0; i < BARDBOX_BUFFER_RAM_RECORDS + 2; ++i) recordOne(b, src, t, i);
  CHECK(b.status().pendingRam == BARDBOX_BUFFER_RAM_RECORDS && b.status().lostRam == 2);  // visible loss
  Record r; uint32_t tok;
  CHECK(!b.peekOldest(r, tok));  // nothing is presented as durable
  CHECK(fs.files.size() == 1 && !fs.formatted);
}

static void test_recovery_handles_interrupted_and_corrupt() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    b.begin(t, 1, &fs);
    recordOne(b, src, t, 1);
    recordOne(b, src, t, 2);
    recordOne(b, src, t, 3);
  }
  fs.files["/bbq/r.tmp"] = {9, 9};                       // power lost before rename
  fs.files["/bbq/r0000000002"].resize(5);                 // torn record
  BardBoxBuffer b;
  b.begin(t, 2, &fs);
  CHECK(b.status().interrupted == 1 && !fs.files.count("/bbq/r.tmp"));
  CHECK(b.status().quarantined == 1 && fs.files.count("/bbq/q0000000002"));
  CHECK(b.status().stored == 2);
  double v; uint32_t tok;
  CHECK(oldest(b, v, tok) && tok == 1 && v == 1);
  b.acknowledge(1);
  CHECK(oldest(b, v, tok) && tok == 3 && v == 3);  // corrupt record never delivered
}

static void test_saturation_keeps_anchor_and_counts_loss() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    b.begin(t, 1, &fs);
    for (int i = 1; i <= 6; ++i) recordOne(b, src, t, i);  // capacity 4
    CHECK(b.status().stored == 4 && b.status().evicted == 2);
    double v; uint32_t tok;
    CHECK(oldest(b, v, tok) && tok == 1 && v == 1);  // oldest unacknowledged anchor kept
  }
  BardBoxBuffer b;
  b.begin(t, 2, &fs);
  CHECK(b.status().evicted == 2);  // loss count survives restart
  double v; uint32_t tok;
  const double expected[] = {1, 4, 5, 6};
  for (double e : expected) {
    CHECK(oldest(b, v, tok) && v == e);
    CHECK(b.acknowledge(tok) == BardBoxBuffer::AckResult::Removed);
  }
}

static void test_lost_meta_never_reuses_tokens() {
  FakeStorage fs;
  ScriptedSource src;
  uint32_t t = 0;
  {
    BardBoxBuffer b;
    b.begin(t, 1, &fs);
    recordOne(b, src, t, 1);
    recordOne(b, src, t, 2);
  }
  fs.files.erase("/bbq/meta");
  BardBoxBuffer b;
  b.begin(t, 2, &fs);
  recordOne(b, src, t, 3);
  double v; uint32_t tok;
  b.acknowledge(1);
  b.acknowledge(2);
  CHECK(oldest(b, v, tok) && tok == 3 && v == 3);
}

static void test_frame_is_separate_and_checked() {
  Record r;
  r.bootId = 7;
  r.seq = 9;
  r.channelCount = 1;
  r.channels[0].value.status = bbcore::ChannelStatus::NonFinite;
  uint8_t frame[bbcore::kMaxFrameBytes];
  const size_t len = bbcore::encodeRecordFrame(r, frame, sizeof frame);
  CHECK(len > 11 && memcmp(frame, "BBQ1", 4) == 0 && frame[11] == bbcore::kPayloadSchemaM1Candidate);
  Record back;
  CHECK(bbcore::decodeRecordFrame(frame, len, back) == bbcore::DecodeResult::Ok);
  CHECK(back.channels[0].value.status == bbcore::ChannelStatus::NonFinite && back.seq == 9);
  frame[len - 1] ^= 1;
  CHECK(bbcore::decodeRecordFrame(frame, len, back) == bbcore::DecodeResult::BadChecksum);
  CHECK(bbcore::decodeRecordFrame(frame, len - 1, back) == bbcore::DecodeResult::BadFrame);
}

int main() {
  test_restart_preserves_order_and_exact_ack();
  test_failures_retain_records();
  test_unavailable_storage_is_never_formatted();
  test_recovery_handles_interrupted_and_corrupt();
  test_saturation_keeps_anchor_and_counts_loss();
  test_lost_meta_never_reuses_tokens();
  test_frame_is_separate_and_checked();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox buffer tests: all checks passed (simulated storage and restarts)\n");
  return 0;
}
