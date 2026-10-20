#include "BardBoxRecordCodec.h"

#include <string.h>

namespace bbcore {

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

namespace {

struct Writer {
  Writer(uint8_t *out, size_t capacity) : p(out), cap(capacity) {}
  uint8_t *p;
  size_t cap;
  size_t n = 0;
  bool ok = true;
  void u8(uint8_t v) {
    if (n + 1 > cap) { ok = false; return; }
    p[n++] = v;
  }
  void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8((v >> (8 * i)) & 0xFF); }
  void u64(uint64_t v) { for (int i = 0; i < 8; ++i) u8((v >> (8 * i)) & 0xFF); }
  void str(const char *s, size_t max) {
    size_t n = strlen(s);
    if (n > max) { ok = false; return; }
    u8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) u8(static_cast<uint8_t>(s[i]));
  }
};

struct Reader {
  Reader(const uint8_t *in, size_t length) : p(in), len(length) {}
  const uint8_t *p;
  size_t len;
  size_t n = 0;
  bool ok = true;
  uint8_t u8() {
    if (n + 1 > len) { ok = false; return 0; }
    return p[n++];
  }
  uint16_t u16() { uint16_t v = u8(); return v | (static_cast<uint16_t>(u8()) << 8); }
  uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(u8()) << (8 * i); return v; }
  uint64_t u64() { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(u8()) << (8 * i); return v; }
  // Reads a length-prefixed string into out (capacity cap, including NUL).
  void str(char *out, size_t cap) {
    const uint8_t n = u8();
    if (n >= cap) { ok = false; return; }
    for (uint8_t i = 0; i < n; ++i) out[i] = static_cast<char>(u8());
    out[ok ? n : 0] = '\0';
  }
};

constexpr size_t kHeaderBytes = 11;  // magic 4, version 1, length 2, crc 4

}  // namespace

size_t encodeRecordFrame(const Record &r, uint8_t *out, size_t cap) {
  if (cap < kHeaderBytes || r.channelCount > kMaxChannels) return 0;
  Writer w(out + kHeaderBytes, cap - kHeaderBytes);
  w.u8(kPayloadSchemaCandidate);
  w.u32(r.queueGeneration);
  w.u32(r.queueSeq);
  w.u32(r.bootId);
  w.u32(r.seq);
  w.u32(r.dueMs);
  w.u32(r.capturedMs);
  w.str(r.attribution.uid, sizeof r.attribution.uid - 1);
  w.str(r.attribution.firmware, sizeof r.attribution.firmware - 1);
  w.str(r.attribution.protocol, sizeof r.attribution.protocol - 1);
  w.u8(r.channelCount);
  for (uint8_t c = 0; c < r.channelCount; ++c) {
    const ChannelSchema &sc = r.schema[c];
    w.str(sc.sensor, sizeof sc.sensor - 1);
    w.str(sc.name, sizeof sc.name - 1);
    w.str(sc.unit, sizeof sc.unit - 1);
    w.u8(static_cast<uint8_t>(sc.type));
    const RecordChannel &ch = r.channels[c];
    w.u8(static_cast<uint8_t>(ch.value.kind));
    w.u8(static_cast<uint8_t>(ch.value.status));
    w.u8((ch.hasSample ? 1 : 0) | (ch.acquiredKnown ? 2 : 0) | (ch.repeatedHandoff ? 4 : 0));
    w.u32(ch.publishSeq);
    w.u32(ch.handoffAgeMs);
    uint64_t bits = 0;
    if (ch.value.kind == ValueKind::Float) memcpy(&bits, &ch.value.f, sizeof bits);
    else if (ch.value.kind == ValueKind::Integer) bits = static_cast<uint64_t>(ch.value.i);
    else bits = ch.value.b ? 1 : 0;
    w.u64(bits);
  }
  if (!w.ok || w.n > 0xFFFF) return 0;
  memcpy(out, "BBQ1", 4);
  out[4] = kFrameVersion;
  out[5] = w.n & 0xFF;
  out[6] = (w.n >> 8) & 0xFF;
  const uint32_t crc = crc32(out + kHeaderBytes, w.n);
  for (int i = 0; i < 4; ++i) out[7 + i] = (crc >> (8 * i)) & 0xFF;
  return kHeaderBytes + w.n;
}

DecodeResult decodeRecordFrame(const uint8_t *data, size_t len, Record &out) {
  if (len < kHeaderBytes || memcmp(data, "BBQ1", 4) != 0 || data[4] != kFrameVersion)
    return DecodeResult::BadFrame;
  const size_t payload = data[5] | (static_cast<size_t>(data[6]) << 8);
  if (kHeaderBytes + payload != len) return DecodeResult::BadFrame;  // torn or padded
  uint32_t crc = 0;
  for (int i = 0; i < 4; ++i) crc |= static_cast<uint32_t>(data[7 + i]) << (8 * i);
  if (crc32(data + kHeaderBytes, payload) != crc) return DecodeResult::BadChecksum;
  Reader r(data + kHeaderBytes, payload);
  if (r.u8() != kPayloadSchemaCandidate) return DecodeResult::UnsupportedSchema;
  static Record rec;  // large; decoding is single-threaded (service task)
  rec = Record();
  rec.queueGeneration = r.u32();
  rec.queueSeq = r.u32();
  rec.bootId = r.u32();
  rec.seq = r.u32();
  rec.dueMs = r.u32();
  rec.capturedMs = r.u32();
  r.str(rec.attribution.uid, sizeof rec.attribution.uid);
  r.str(rec.attribution.firmware, sizeof rec.attribution.firmware);
  r.str(rec.attribution.protocol, sizeof rec.attribution.protocol);
  rec.channelCount = r.u8();
  if (!r.ok || rec.channelCount > kMaxChannels) return DecodeResult::BadFrame;
  for (uint8_t c = 0; c < rec.channelCount; ++c) {
    ChannelSchema &sc = rec.schema[c];
    r.str(sc.sensor, sizeof sc.sensor);
    r.str(sc.name, sizeof sc.name);
    r.str(sc.unit, sizeof sc.unit);
    const uint8_t type = r.u8();
    if (type > static_cast<uint8_t>(ValueKind::Boolean)) return DecodeResult::BadFrame;
    sc.type = static_cast<ValueKind>(type);
    RecordChannel &ch = rec.channels[c];
    const uint8_t kind = r.u8(), status = r.u8(), flags = r.u8();
    if (kind > static_cast<uint8_t>(ValueKind::Boolean) ||
        status > static_cast<uint8_t>(ChannelStatus::NotRepresentable))
      return DecodeResult::BadFrame;
    ch.value.kind = static_cast<ValueKind>(kind);
    ch.value.status = static_cast<ChannelStatus>(status);
    ch.hasSample = flags & 1;
    ch.acquiredKnown = flags & 2;
    ch.repeatedHandoff = flags & 4;
    ch.publishSeq = r.u32();
    ch.handoffAgeMs = r.u32();
    const uint64_t bits = r.u64();
    if (ch.value.kind == ValueKind::Float) memcpy(&ch.value.f, &bits, sizeof bits);
    else if (ch.value.kind == ValueKind::Integer) ch.value.i = static_cast<int64_t>(bits);
    else ch.value.b = bits != 0;
  }
  if (!r.ok || r.n != payload) return DecodeResult::BadFrame;
  out = rec;
  return DecodeResult::Ok;
}

}  // namespace bbcore
