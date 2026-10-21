#include "BardBoxPayload.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace bbcore {

namespace {

class JsonOut {
 public:
  JsonOut(char *buf, size_t cap) : buf_(buf), cap_(cap) {}
  void raw(const char *s) { bytes(s, strlen(s)); }
  void format(const char *fmt, ...) {
    char tmp[40];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(tmp, sizeof tmp, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof tmp) {
      overflow_ = true;
      return;
    }
    bytes(tmp, static_cast<size_t>(n));
  }
  // A JSON string; every byte outside printable ASCII is escaped.
  void string(const char *s) {
    raw("\"");
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p) {
      if (*p == '"' || *p == '\\') {
        const char esc[3] = {'\\', static_cast<char>(*p), 0};
        raw(esc);
      } else if (*p < 0x20 || *p > 0x7e) {
        format("\\u%04x", *p);
      } else {
        bytes(reinterpret_cast<const char *>(p), 1);
      }
    }
    raw("\"");
  }
  size_t length() const { return overflow_ ? 0 : len_; }

 private:
  void bytes(const char *s, size_t n) {
    if (overflow_ || len_ + n >= cap_) {
      overflow_ = true;
      return;
    }
    memcpy(buf_ + len_, s, n);
    len_ += n;
    buf_[len_] = 0;
  }
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

const char *typeName(ValueKind kind) {
  switch (kind) {
    case ValueKind::Integer: return "integer";
    case ValueKind::Boolean: return "boolean";
    default: return "float";
  }
}

// Fixed-size copy of a stored field, so a damaged record cannot overrun.
template <size_t N>
const char *bounded(const char (&field)[N], char (&copy)[N]) {
  memcpy(copy, field, N);
  copy[N - 1] = 0;
  return copy;
}

}  // namespace

// "name":"text" or "name":null when unavailable.
static void textOrNull(JsonOut &j, const char *name, const char *value) {
  j.raw(",\"");
  j.raw(name);
  j.raw("\":");
  if (*value) j.string(value);
  else j.raw("null");
}

size_t serializeDeliveryCandidate(const Record &r, const QueueToken &token, char *out, size_t cap,
                                  const ConnectivityStatus *sender) {
  if (!out || cap == 0 || r.channelCount > kMaxChannels) return 0;
  JsonOut j(out, cap);
  char uid[sizeof r.attribution.uid], fw[sizeof r.attribution.firmware], proto[sizeof r.attribution.protocol];
  j.raw("{\"format\":\"bardbox-delivery-candidate\",\"queue\":{\"generation\":");
  j.format("%lu", static_cast<unsigned long>(token.generation));
  j.raw(",\"seq\":");
  j.format("%lu", static_cast<unsigned long>(token.seq));
  j.raw("},\"uid\":");
  j.string(bounded(r.attribution.uid, uid));
  j.raw(",\"firmware\":");
  j.string(bounded(r.attribution.firmware, fw));
  j.raw(",\"protocol\":");
  j.string(bounded(r.attribution.protocol, proto));
  j.format(",\"boot\":\"%08lx\"", static_cast<unsigned long>(r.bootId));
  j.format(",\"record\":%lu", static_cast<unsigned long>(r.seq));
  j.format(",\"due_ms\":%lu", static_cast<unsigned long>(r.dueMs));
  j.format(",\"captured_ms\":%lu", static_cast<unsigned long>(r.capturedMs));
  j.raw(",\"acquired_time\":\"unknown\",\"channels\":[");
  for (uint8_t c = 0; c < r.channelCount; ++c) {
    const ChannelSchema &s = r.schema[c];
    const RecordChannel &ch = r.channels[c];
    char sensor[sizeof s.sensor], name[sizeof s.name], unit[sizeof s.unit];
    j.raw(c ? ",{\"sensor\":" : "{\"sensor\":");
    j.string(bounded(s.sensor, sensor));
    j.raw(",\"measurement\":");
    j.string(bounded(s.name, name));
    j.raw(",\"unit\":");
    j.string(bounded(s.unit, unit));
    j.raw(",\"type\":\"");
    j.raw(typeName(s.type));
    j.raw("\",\"status\":\"");
    j.raw(channelStatusCode(ch.value.status));
    j.raw("\",\"value\":");
    if (ch.value.status != ChannelStatus::Valid) {
      j.raw("null");
    } else if (ch.value.kind == ValueKind::Integer) {
      j.format("%lld", static_cast<long long>(ch.value.i));
    } else if (ch.value.kind == ValueKind::Boolean) {
      j.raw(ch.value.b ? "true" : "false");
    } else {
      j.format("%.17g", ch.value.f);
    }
    j.format(",\"publish_seq\":%lu", static_cast<unsigned long>(ch.publishSeq));
    j.format(",\"handoff_age_ms\":%lu", static_cast<unsigned long>(ch.handoffAgeMs));
    j.raw(ch.repeatedHandoff ? ",\"repeated_handoff\":true}" : ",\"repeated_handoff\":false}");
  }
  j.raw("]");
  if (sender) {
    j.raw(",\"device_status\":{\"at\":\"send\",\"wifi\":\"");
    j.raw(wifiStateName(sender->wifi));
    j.raw("\"");
    textOrNull(j, "ip", sender->ip);
    textOrNull(j, "mac", sender->mac);
    if (sender->rssiKnown) j.format(",\"rssi_dbm\":%d", sender->rssiDbm);
    else j.raw(",\"rssi_dbm\":null");
    textOrNull(j, "wifi_ssid", sender->ssid);
    textOrNull(j, "receiver", sender->receiver);
    j.raw("}");
  }
  j.raw("}");
  return j.length();
}

}  // namespace bbcore
