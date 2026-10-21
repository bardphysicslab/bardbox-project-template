#include "BardBoxSerial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace bbcore {

SerialLine &SerialLine::text(const char *s) {
  const size_t n = strlen(s);
  if (overflow_ || len_ + n > cap_) {
    overflow_ = true;
    return *this;
  }
  memcpy(buf_ + len_, s, n);
  len_ += n;
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

void SerialLine::end() { text("\n"); }

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
  if (len > 7 && equalsIgnoreCase(line, 7, "CONFIG_")) return SerialCommand::Config;
  return SerialCommand::Unknown;
}

void BardBoxSerial::begin(Stream &port, SerialDataSource &source) {
  port_ = &port;
  source_ = &source;
  len_ = 0;
  overflow_ = false;
}

bool BardBoxSerial::flush() {
  while (outPos_ < outLen_) {
    const int room = port_->availableForWrite();
    if (room <= 0) {
      ++stalled_;  // host is not draining; try again next step
      return false;
    }
    size_t n = outLen_ - outPos_;
    if (n > static_cast<size_t>(room)) n = static_cast<size_t>(room);
    const size_t written = port_->write(reinterpret_cast<const uint8_t *>(out_ + outPos_), n);
    if (written == 0) {
      ++stalled_;
      return false;
    }
    outPos_ += written;
  }
  outLen_ = outPos_ = 0;
  return true;
}

void BardBoxSerial::service(uint8_t maxCommands) {
  if (!port_) return;
  if (!flush()) return;  // finish the pending reply before reading more
  uint8_t answered = 0;
  size_t bytes = 0;
  while (answered < maxCommands && bytes < kMaxBytesPerStep && port_->available() > 0) {
    int c = port_->read();
    if (c < 0) break;
    ++bytes;
    if (c == '\r') continue;
    if (c == '\n') {
      const bool complete = !overflow_;
      buf_[len_] = '\0';
      len_ = 0;
      overflow_ = false;
      if (!complete) {
        ++discarded_;
        continue;
      }
      handle(buf_);  // buf_ still holds the finished line
      ++answered;
      if (!flush()) return;  // input state is already reset for the next line
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
  SerialLine out(out_, kMaxReply);
  switch (parseSerialCommand(line)) {
    case SerialCommand::Empty: return;
    case SerialCommand::Info: writeInfo(out); break;
    case SerialCommand::Header: writeHeader(out); break;
    case SerialCommand::Read: writeRead(out); break;
    case SerialCommand::ReadLive: writeReadLive(out); break;
    case SerialCommand::Ping: out.text("PONG").end(); break;
    case SerialCommand::Config:
      if (settings_) {
        handleConfig(line, out);
        break;
      }
      out.text("ERR UNKNOWN_CMD").end();  // firmware without the settings capability
      break;
    case SerialCommand::Unknown: out.text("ERR UNKNOWN_CMD").end(); break;
  }
  if (out.overflowed()) {
    SerialLine err(out_, kMaxReply);
    err.text("ERR REPLY_TOO_LONG").end();
    outLen_ = err.length();
  } else {
    outLen_ = out.length();
  }
  outPos_ = 0;
}

// ---- Device settings (capability config/1, a candidate pending review) ----
//
// CONFIG_SCHEMA                fields, bounds, encoding and activation rule
// CONFIG_GET                   saved and active public values; password presence only
// CONFIG_BEGIN <revision>      start an update against the revision just read
// CONFIG_SET <field> <hex|->   one field, value as hex bytes ("-" = empty)
// CONFIG_COMMIT                validate all fields and save one record
// CONFIG_ABORT                 discard the update
// Values are never echoed; a password is reported only as set or empty.

namespace {

// Splits off the next space-separated word; returns its length (0 at end).
size_t nextWord(const char *&p, const char *&word) {
  while (*p == ' ' || *p == '\t') ++p;
  word = p;
  while (*p && *p != ' ' && *p != '\t') ++p;
  return static_cast<size_t>(p - word);
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "-" is the empty value. Returns false for odd length or a non-hex digit.
bool decodeHex(const char *s, size_t n, char *out, size_t cap, size_t &len) {
  len = 0;
  if (n == 1 && s[0] == '-') return true;
  if (n == 0 || n % 2 || n / 2 > cap) return false;
  for (size_t i = 0; i < n; i += 2) {
    const int hi = hexDigit(s[i]), lo = hexDigit(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[len++] = static_cast<char>(hi * 16 + lo);
  }
  return true;
}

void hexText(SerialLine &line, const char *s) {
  if (!*s) {
    line.text("-");
    return;
  }
  char pair[3];
  for (; *s; ++s) {
    snprintf(pair, sizeof pair, "%02x", static_cast<unsigned char>(*s));
    line.text(pair);
  }
}

const char *storeStateName(BardBoxSettings::StoreState s) {
  switch (s) {
    case BardBoxSettings::StoreState::None: return "none";
    case BardBoxSettings::StoreState::Saved: return "saved";
    case BardBoxSettings::StoreState::Invalid: return "invalid";
    default: return "unavailable";
  }
}

bool parseRevision(const char *s, size_t n, uint32_t &out) {
  if (n == 0 || n > 10) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + static_cast<uint64_t>(s[i] - '0');
  }
  if (v > 0xffffffffull) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

void BardBoxSerial::handleConfig(const char *line, SerialLine &out) {
  BardBoxSettings &st = *settings_;
  const char *p = line, *word = nullptr, *arg = nullptr, *extra = nullptr;
  const size_t n = nextWord(p, word);
  size_t argLen = nextWord(p, arg);
  const char *value = nullptr;
  const size_t valueLen = nextWord(p, value);
  const bool trailing = nextWord(p, extra) > 0;
  const uint32_t now = millis();
  typedef BardBoxSettings::Result R;

  if (equalsIgnoreCase(word, n, "CONFIG_SCHEMA") && !argLen) {
    out.text("CONFIG_SCHEMA,version=1,encoding=hex,activation=next_boot,fields=")
        .text("uid:1:32:public;wifi_ssid:0:32:public;wifi_password:8:63:secret:empty_open").end();
    return;
  }
  if (equalsIgnoreCase(word, n, "CONFIG_GET") && !argLen) {
    const SettingsValues &next = st.next(), &active = st.active();
    out.text("CONFIG,version=1,store=").text(storeStateName(st.storeState())).text(",revision=").uint(st.revision());
    out.text(",uid=").text(next.uid).text(",wifi_ssid=");
    hexText(out, next.ssid);
    out.text(",wifi_password=").text(next.password[0] ? "set" : "empty");
    out.text(",active_uid=").text(active.uid).text(",active_wifi_ssid=");
    hexText(out, active.ssid);
    out.text(",active_wifi_password=").text(active.password[0] ? "set" : "empty");
    out.text(",pending=").text(st.pending() ? "1" : "0").text(",activation=next_boot").end();
    return;
  }
  R result;
  BardBoxSettings::Field field = BardBoxSettings::Field::Unknown;
  if (equalsIgnoreCase(word, n, "CONFIG_BEGIN") && argLen && !valueLen) {
    uint32_t revision = 0;
    if (!parseRevision(arg, argLen, revision)) {
      out.text("ERR,CONFIG_COMMAND").end();
      return;
    }
    result = st.beginUpdate(revision, now);
    if (result == R::Ok) {
      out.text("CONFIG_BEGIN_OK,revision=").uint(st.revision()).end();
      return;
    }
  } else if (equalsIgnoreCase(word, n, "CONFIG_SET") && argLen && valueLen && !trailing) {
    char name[16] = {};
    if (argLen < sizeof name) memcpy(name, arg, argLen);
    field = BardBoxSettings::fieldNamed(name);
    char decoded[64];
    size_t len = 0;
    if (field != BardBoxSettings::Field::Unknown && !decodeHex(value, valueLen, decoded, sizeof decoded, len))
      result = st.rejectValue(now);
    else
      result = st.setField(field, decoded, len, now);
    if (result == R::Ok) {
      out.text("CONFIG_SET_OK,field=").text(BardBoxSettings::fieldName(field)).end();
      return;
    }
  } else if (equalsIgnoreCase(word, n, "CONFIG_COMMIT") && !argLen) {
    result = st.commit(now, field);
    if (result == R::Ok) {
      out.text("CONFIG_COMMIT_OK,revision=").uint(st.revision()).text(",activation=next_boot").end();
      return;
    }
  } else if (equalsIgnoreCase(word, n, "CONFIG_ABORT") && !argLen) {
    st.abort();
    out.text("CONFIG_ABORT_OK").end();
    return;
  } else {
    out.text("ERR,CONFIG_COMMAND").end();
    return;
  }
  switch (result) {
    case R::Unavailable: out.text("ERR,CONFIG_UNAVAILABLE").end(); break;
    case R::StaleRevision: out.text("ERR,CONFIG_REVISION,revision=").uint(st.revision()).end(); break;
    case R::NoTransaction: out.text("ERR,CONFIG_NO_TRANSACTION").end(); break;
    case R::Expired: out.text("ERR,CONFIG_EXPIRED").end(); break;
    case R::UnknownField: out.text("ERR,CONFIG_FIELD").end(); break;
    case R::InvalidValue: out.text("ERR,CONFIG_VALUE,field=").text(BardBoxSettings::fieldName(field)).end(); break;
    default: out.text("ERR,CONFIG_STORE,revision=").uint(st.revision()).end(); break;
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
  line.text(" wire=").text(kWireProtocol).text(" boot=").hex32(st.bootId).text(" recording_s=").uint(st.recordingIntervalS);
  Record &r = record_;
  if (source_->latestRecord(r)) {
    line.text(" record=").uint(r.seq).text(" record_ms=").uint(r.capturedMs);
  } else {
    line.text(" record=none");
  }
  line.text(" storage=").text(st.storage).text(st.durable ? " durable=yes" : " durable=no");
  line.text(" stored=").uint(st.stored).text(" capacity=").uint(st.capacity);
  line.text(" evicted=").uint(st.evicted).text(" pending_ram=").uint(st.pendingRam);
  line.text(" lost_ram=").uint(st.lostRam).text(" write_failures=").uint(st.writeFailures);
  line.text(" interrupted=").uint(st.interrupted).text(" quarantined=").uint(st.quarantined);
  line.text(" skipped=").uint(st.skippedBoundaries).text(" queue=").text(st.queueState);
  line.text(" hidden=").uint(st.hidden).text(" unreadable=").uint(st.unreadable);
  line.text(" meta_rebuilt=").uint(st.metaRebuilt);
  line.text(st.lossHistoryKnown ? " loss_history=known" : " loss_history=unknown");
  line.text(" net=").text(st.network).text(" net_attempts=").uint(st.netAttempts);
  line.text(" net_acked=").uint(st.netAcked).text(" net_failed=").uint(st.netFailed);
  line.text(" net_timeouts=").uint(st.netTimeouts).text(" net_retry_ms=").uint(st.netRetryMs);
  line.text(" net_last=").text(st.netLast);
  line.text(" acquired_time=unknown reg_errors=").uint(st.registrationErrors);
  // Wi-Fi diagnostics (template#36): same names and meanings as the delivery
  // body's device_status; "-" = unavailable; SSID hex-encoded as in CONFIG_GET.
  const ConnectivityStatus &w = st.link;
  line.text(" wifi=").text(wifiStateName(w.wifi)).text(" ip=").text(w.ip[0] ? w.ip : "-");
  line.text(" mac=").text(w.mac[0] ? w.mac : "-").text(" rssi_dbm=");
  if (w.rssiKnown) line.integer(w.rssiDbm);
  else line.text("-");
  line.text(" wifi_ssid=");
  hexText(line, w.ssid);
  line.text(" receiver=").text(w.receiver[0] ? w.receiver : "-");
  line.text(" net_error=").text(st.netError);  // last connection failure: link|dns|tcp|tls:-0x..:0x..|send
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
  Record &r = record_;
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
