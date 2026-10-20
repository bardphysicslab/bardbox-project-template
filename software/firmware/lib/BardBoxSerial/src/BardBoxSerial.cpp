#include "BardBoxSerial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace bbcore {

SerialLine &SerialLine::text(const char *s) {
  out_.write(reinterpret_cast<const uint8_t *>(s), strlen(s));
  return *this;
}

SerialLine &SerialLine::uint(uint32_t v) {
  char b[12];
  snprintf(b, sizeof b, "%lu", static_cast<unsigned long>(v));
  return text(b);
}

SerialLine &SerialLine::integer(int64_t v) {
  char b[24];
  snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
  return text(b);
}

SerialLine &SerialLine::real(double v) {
  char b[32];
  snprintf(b, sizeof b, "%.6g", v);
  return text(b);
}

SerialLine &SerialLine::hex32(uint32_t v) {
  char b[9];
  snprintf(b, sizeof b, "%08lx", static_cast<unsigned long>(v));
  return text(b);
}

void SerialLine::end() { out_.write(reinterpret_cast<const uint8_t *>("\n"), 1); }

static bool equalsIgnoreCase(const char *a, size_t alen, const char *b) {
  if (alen != strlen(b)) return false;
  for (size_t i = 0; i < alen; ++i)
    if (toupper(static_cast<unsigned char>(a[i])) != b[i]) return false;
  return true;
}

SerialCommand parseSerialCommand(const char *line) {
  while (*line == ' ' || *line == '\t') ++line;
  size_t len = strlen(line);
  while (len && (line[len - 1] == ' ' || line[len - 1] == '\t')) --len;
  if (len == 0) return SerialCommand::Empty;
  if (equalsIgnoreCase(line, len, "INFO")) return SerialCommand::Info;
  if (equalsIgnoreCase(line, len, "HEADER")) return SerialCommand::Header;
  if (equalsIgnoreCase(line, len, "READ")) return SerialCommand::Read;
  if (equalsIgnoreCase(line, len, "READ_LIVE")) return SerialCommand::ReadLive;
  if (equalsIgnoreCase(line, len, "PING")) return SerialCommand::Ping;
  return SerialCommand::Unknown;
}

void BardBoxSerial::begin(Stream &port, SerialDataSource &source) {
  port_ = &port;
  source_ = &source;
  len_ = 0;
  overflow_ = false;
}

void BardBoxSerial::service(uint8_t maxCommands) {
  if (!port_) return;
  uint8_t answered = 0;
  size_t bytes = 0;
  while (answered < maxCommands && bytes < kMaxBytesPerStep && port_->available() > 0) {
    int c = port_->read();
    if (c < 0) break;
    ++bytes;
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow_) {
        ++discarded_;
      } else {
        buf_[len_] = '\0';
        handle(buf_);
        ++answered;
      }
      len_ = 0;
      overflow_ = false;
      continue;
    }
    if (len_ < kMaxLine) {
      buf_[len_++] = static_cast<char>(c);
    } else {
      overflow_ = true;  // discard through the next newline
    }
  }
}

void BardBoxSerial::handle(const char *line) {
  SerialLine out(*port_);
  switch (parseSerialCommand(line)) {
    case SerialCommand::Empty: return;
    case SerialCommand::Info: writeInfo(out); return;
    case SerialCommand::Header: writeHeader(out); return;
    case SerialCommand::Read: writeRead(out); return;
    case SerialCommand::ReadLive: writeReadLive(out); return;
    case SerialCommand::Ping: out.text("PONG").end(); return;
    case SerialCommand::Unknown: out.text("ERR UNKNOWN_CMD").end(); return;
  }
}

void BardBoxSerial::writeChannelName(SerialLine &line, uint8_t c) {
  line.text(source_->sensorName(source_->channelSensor(c))).text(".").text(source_->channelName(c));
}

void BardBoxSerial::writeValue(SerialLine &line, const ChannelValue &v) {
  if (v.status != ChannelStatus::Valid) {
    line.text("!").text(channelStatusCode(v.status));
    return;
  }
  switch (v.kind) {
    case ValueKind::Float: line.real(v.f); break;
    case ValueKind::Integer: line.integer(v.i); break;
    case ValueKind::Boolean: line.text(v.b ? "true" : "false"); break;
  }
}

// OK INFO uid=<uid> fw=<version> sensors=<a,b|none> + additive diagnostics.
void BardBoxSerial::writeInfo(SerialLine &line) {
  SerialStatus st;
  source_->status(st);
  line.text("OK INFO uid=").text(st.uid).text(" fw=").text(st.firmware).text(" sensors=");
  const uint8_t sensors = source_->sensorCount();
  if (sensors == 0) line.text("none");
  for (uint8_t s = 0; s < sensors; ++s) line.text(s ? "," : "").text(source_->sensorName(s));
  line.text(" wire=v2-candidate boot=").hex32(st.bootId).text(" recording_s=").uint(st.recordingIntervalS);
  Record r;
  if (source_->latestRecord(r)) {
    line.text(" record=").uint(r.seq).text(" record_ms=").uint(r.capturedMs);
  } else {
    line.text(" record=none");
  }
  line.text(" buffered=").uint(st.buffered).text(" backpressure=").uint(st.backpressure);
  line.text(" skipped=").uint(st.skippedBoundaries).text(" storage=").text(st.storage);
  line.text(" net=").text(st.network).text(" acquired_time=unknown reg_errors=").uint(st.registrationErrors);
  line.end();
}

// HDR,v2,record,boot,captured_ms,<sensor.measurement>...
void BardBoxSerial::writeHeader(SerialLine &line) {
  line.text("HDR,v2,record,boot,captured_ms");
  const uint8_t channels = source_->channelCount();
  for (uint8_t c = 0; c < channels; ++c) {
    line.text(",");
    writeChannelName(line, c);
  }
  line.end();
}

// Latest completed record: DAT,v2,<record>,<boot>,<captured_ms>,<value|!code>...
// Healthy channels keep their values; a bad channel shows its code.
void BardBoxSerial::writeRead(SerialLine &line) {
  Record r;
  if (source_->channelCount() == 0) {
    line.text("ERR NO_SENSOR").end();
    return;
  }
  if (!source_->latestRecord(r)) {
    line.text("ERR SAMPLE_NOT_READY").end();
    return;
  }
  line.text("DAT,v2,").uint(r.seq).text(",").hex32(r.bootId).text(",").uint(r.capturedMs);
  for (uint8_t c = 0; c < r.channelCount; ++c) {
    line.text(",");
    writeValue(line, r.channels[c].value);
  }
  line.end();
}

// Latest cache: LIVE,v2,<boot>,<now_ms>|<sensor>,publish=<n|none>,
// handoff_age_ms=<n>,acquired=unknown,<measurement>=<value|!code>...
void BardBoxSerial::writeReadLive(SerialLine &line) {
  RecordChannel channels[kMaxChannels];
  uint32_t nowMs = 0;
  const uint8_t count = source_->liveChannels(channels, nowMs);
  SerialStatus st;
  source_->status(st);
  line.text("LIVE,v2,").hex32(st.bootId).text(",").uint(nowMs);
  const uint8_t sensors = source_->sensorCount();
  for (uint8_t s = 0; s < sensors; ++s) {
    line.text("|").text(source_->sensorName(s));
    bool header = false;
    for (uint8_t c = 0; c < count; ++c) {
      if (source_->channelSensor(c) != s) continue;
      const RecordChannel &ch = channels[c];
      if (!header) {
        if (!ch.hasSample) {
          line.text(",publish=none");
          break;
        }
        line.text(",publish=").uint(ch.publishSeq).text(",handoff_age_ms=").uint(ch.handoffAgeMs);
        line.text(",acquired=unknown");
        header = true;
      }
      line.text(",").text(source_->channelName(c)).text("=");
      writeValue(line, ch.value);
    }
  }
  line.end();
}

}  // namespace bbcore
