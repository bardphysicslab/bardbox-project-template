// The three FW-M2 review reproductions (Codex, against 9a1a571), adapted to the
// corrected API (QueueToken, begin with a generation seed). Capacity 4.
#include <stdio.h>
#include <string.h>

#include "BardBoxBuffer.h"
#include "BardBoxRecordCodec.h"
#include "FakeStorage.h"

using bbcore::BardBoxBuffer;
using bbcore::QueueToken;
using bbcore::Record;

struct Src : bbcore::SnapshotSource {
  double value = 0;
  uint32_t now = 0, pub = 0;
  uint8_t snapshotChannels(bbcore::RecordChannel *out, uint32_t &captured) override {
    captured = now;
    out[0] = bbcore::RecordChannel();
    out[0].hasSample = true;
    out[0].publishSeq = ++pub;
    out[0].value.status = bbcore::ChannelStatus::Valid;
    out[0].value.f = value;
    return 1;
  }
  void describeChannels(bbcore::ChannelSchema *out) override { out[0] = bbcore::ChannelSchema(); }
};

static void one(BardBoxBuffer &b, Src &s, uint32_t &t, double v) {
  t += BardBoxBuffer::recordingIntervalMs();
  s.now = t;
  s.value = v;
  b.service(t, s);
}

int main() {
  {
    FakeStorage fs; Src src; uint32_t t = 0; QueueToken old;
    { BardBoxBuffer b; b.begin(t, 1, &fs, 101); one(b, src, t, 1); Record r; b.peekOldest(r, old); b.acknowledge(old); }
    fs.files.erase("/bbq/meta");
    BardBoxBuffer b; b.begin(t, 2, &fs, 202); one(b, src, t, 2);
    Record r; QueueToken now; b.peekOldest(r, now);
    const int ack = (int)b.acknowledge(old);
    printf("lost meta after drain: old_token=%lu/%lu new_token=%lu/%lu stale_ack_result=%d(%s) remaining=%lu\n",
           (unsigned long)old.generation, (unsigned long)old.seq, (unsigned long)now.generation,
           (unsigned long)now.seq, ack, ack == 1 ? "StaleToken" : "other", (unsigned long)b.status().stored);
  }
  {
    FakeStorage fs; fs.dirs.insert("/bbq");
    for (unsigned i = 1; i <= 5; i++) {
      static Record r; r = Record(); r.queueGeneration = 9; r.queueSeq = i;
      uint8_t frame[bbcore::kMaxFrameBytes];
      const size_t n = bbcore::encodeRecordFrame(r, frame, sizeof frame);
      char p[30]; snprintf(p, sizeof p, "/bbq/r%010u", i); fs.writeFile(p, frame, n);
    }
    BardBoxBuffer b; b.begin(0, 1, &fs, 303);
    printf("capacity reduction at start: stored=%lu hidden=%lu queue=%s\n", (unsigned long)b.status().stored,
           (unsigned long)b.status().hidden, bbcore::queueStateName(b.status().state));
    Record r; QueueToken tok; unsigned delivered = 0;
    while (b.peekOldest(r, tok)) { b.acknowledge(tok); ++delivered; }
    printf("capacity reduction after drain: delivered=%u stored=%lu fifth_file_exists=%d peek=%d queue=%s\n",
           delivered, (unsigned long)b.status().stored, (int)fs.exists("/bbq/r0000000005"),
           (int)b.peekOldest(r, tok), bbcore::queueStateName(b.status().state));
  }
  {
    FakeStorage fs; Src src; uint32_t t = 0;
    { BardBoxBuffer b; b.begin(0, 1, &fs, 404); for (int i = 0; i < 4; i++) one(b, src, t, i);
      fs.failWriteAt = 4;  // the record write after the eviction
      one(b, src, t, 4);
      printf("eviction then failed append: evicted=%lu pending_ram=%lu\n", (unsigned long)b.status().evicted,
             (unsigned long)b.status().pendingRam); }
    BardBoxBuffer b; b.begin(t, 2, &fs, 505);
    printf("after restart: evicted=%lu victim2_exists=%d\n", (unsigned long)b.status().evicted,
           (int)fs.exists("/bbq/r0000000002"));
  }
  return 0;
}
