#include "BardBoxWifiTransport.h"
#include <stdio.h>
#include <string.h>

namespace bbcore {
namespace {
bool visible(const char *s, size_t max) {
  if (!s) return false;
  size_t n = 0;
  for (; s[n]; ++n)
    if (n >= max || static_cast<unsigned char>(s[n]) < 33 ||
        static_cast<unsigned char>(s[n]) > 126) return false;
  return n > 0;
}
bool equalAscii(const char *a, const char *b) {
  while (*a && *b) {
    const unsigned char x = static_cast<unsigned char>(*a++);
    const unsigned char y = static_cast<unsigned char>(*b++);
    if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y)) return false;
  }
  return !*a && !*b;
}
bool tokenChar(unsigned char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || strchr("!#$%&'*+-.^_`|~", ch);
}
}
WifiHttpsTransport::WifiHttpsTransport(WifiIo &io, const WifiSettings &s) : io_(io), settings_(s) {
  configured_ = s.ssid && *s.ssid && strlen(s.ssid) <= 32 &&
                (!s.password || strlen(s.password) <= 64) && s.caPem && *s.caPem &&
                strlen(s.caPem) <= 16384 && visible(s.ntpServer, 127) &&
                (!s.bearer || !*s.bearer || visible(s.bearer, 256)) &&
                s.reconnectMs >= 1000 && s.reconnectMs <= 3600000 && parseEndpoint();
}

bool WifiHttpsTransport::parseEndpoint() {
  const char *url = settings_.endpoint;
  if (!url || strncmp(url, "https://", 8) || !visible(url, 510)) return false;
  const char *p = url + 8;
  size_t n = 0;
  for (; *p && *p != ':' && *p != '/'; ++p) {
    const unsigned char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '.') || n >= sizeof host_ - 1) return false;
    host_[n++] = c;
  }
  if (!n || host_[0] == '.' || host_[n - 1] == '.') return false;
  host_[n] = 0;
  if (*p == ':') {
    ++p;
    unsigned port = 0, digits = 0;
    for (; *p && *p != '/'; ++p) {
      if (*p < '0' || *p > '9' || ++digits > 5) return false;
      port = port * 10 + (*p - '0');
    }
    if (!digits || port == 0 || port > 65535) return false;
    port_ = static_cast<uint16_t>(port);
  }
  if (!*p) p = "/";
  if (strlen(p) >= sizeof target_ || strchr(p, '#')) return false;
  strcpy(target_, p);
  return true;
}

bool WifiHttpsTransport::start(uint32_t id, const uint8_t *body, size_t len, uint32_t) {
  if (!configured_ || active_ || !body || !len || len > kMaxDeliveryBody || !io_.reset()) return false;
  char authority[140];
  if (port_ == 443) snprintf(authority, sizeof authority, "%s", host_);
  else snprintf(authority, sizeof authority, "%s:%u", host_, port_);
  const char *bearer = settings_.bearer && *settings_.bearer ? settings_.bearer : nullptr;
  const int n = snprintf(request_, sizeof request_,
      "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
      "Content-Length: %u\r\nConnection: close\r\n%s%s%s\r\n",
      target_, authority, static_cast<unsigned>(len), bearer ? "Authorization: Bearer " : "",
      bearer ? bearer : "", bearer ? "\r\n" : "");
  if (n < 0 || static_cast<size_t>(n) + len >= sizeof request_) return false;
  memcpy(request_ + n, body, len);
  requestLen_ = static_cast<size_t>(n) + len;
  sent_ = responseBytes_ = lineLen_ = 0;
  firstLine_ = true;
  previousCr_ = false;
  responseStatus_ = 0;
  contentLength_ = 0;
  hasContentLength_ = hasTransferEncoding_ = http11_ = false;
  result_ = TransportOutcome();
  attemptId_ = id;
  active_ = true;
  phase_ = TransportPhase::Associating;
  return true;
}

void WifiHttpsTransport::service(uint32_t now) {
  if (!configured_) return;
  if (io_.connected()) { io_.clockReady(settings_.ntpServer); return; }
  if (!haveAssociationTry_ || now - lastAssociationTry_ >= settings_.reconnectMs) {
    haveAssociationTry_ = true;
    lastAssociationTry_ = now;
    io_.associate(settings_.ssid, settings_.password ? settings_.password : "");
  }
}

void WifiHttpsTransport::abort() {
  io_.close();
  active_ = false;
  phase_ = TransportPhase::Idle;
  // The request can contain a credential; don't leave it in a reusable reply.
  memset(request_, 0, sizeof request_);
}

void WifiHttpsTransport::finish(TransportOutcome::Kind kind, int status) {
  io_.close();
  result_.kind = kind;
  result_.httpStatus = status;
  result_.attemptId = attemptId_;
  result_.endpointVerified = kind == TransportOutcome::Kind::HttpStatus;
  phase_ = TransportPhase::Done;
  active_ = false;
  memset(request_, 0, sizeof request_);
}

// Strict CRLF framing, bounded headers, and no incomplete status-only ACK.
bool WifiHttpsTransport::responseByte(uint8_t ch) {
  if (++responseBytes_ > 8192 || (previousCr_ && ch != '\n')) {
    finish(TransportOutcome::Kind::Malformed); return false;
  }
  if (ch == '\r') { previousCr_ = true; return true; }
  if (ch == '\n') {
    if (!previousCr_) { finish(TransportOutcome::Kind::Malformed); return false; }
    previousCr_ = false;
    line_[lineLen_] = 0;
    if (firstLine_) {
      if (lineLen_ < 12 || (strncmp(line_, "HTTP/1.1 ", 9) && strncmp(line_, "HTTP/1.0 ", 9)) ||
          line_[9] < '1' || line_[9] > '5' || line_[10] < '0' || line_[10] > '9' ||
          line_[11] < '0' || line_[11] > '9' || (lineLen_ > 12 && line_[12] != ' ')) {
        finish(TransportOutcome::Kind::Malformed); return false;
      }
      responseStatus_ = (line_[9]-'0')*100 + (line_[10]-'0')*10 + line_[11]-'0';
      http11_ = !strncmp(line_, "HTTP/1.1 ", 9);
      firstLine_ = false;
    } else if (!lineLen_) {
      if ((hasTransferEncoding_ && (hasContentLength_ || !http11_)) ||
          (responseStatus_ == 204 && (hasContentLength_ || hasTransferEncoding_))) {
        finish(TransportOutcome::Kind::Malformed); return false;
      }
      finish(TransportOutcome::Kind::HttpStatus, responseStatus_); return false;
    } else {
      size_t i = 0;
      while (i < lineLen_ && line_[i] != ':' && tokenChar(line_[i])) ++i;
      if (!i || i == lineLen_ || line_[i] != ':') {
        finish(TransportOutcome::Kind::Malformed); return false;
      }
      line_[i] = 0;
      char *value = line_ + i + 1;
      while (*value == ' ' || *value == '\t') ++value;
      char *end = line_ + lineLen_;
      while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
      *end = 0;
      if (equalAscii(line_, "Content-Length")) {
        uint64_t length = 0;
        if (!*value) { finish(TransportOutcome::Kind::Malformed); return false; }
        for (const char *p = value; *p; ++p) {
          if (*p < '0' || *p > '9' || length > (UINT64_MAX - (*p - '0')) / 10) {
            finish(TransportOutcome::Kind::Malformed); return false;
          }
          length = length * 10 + (*p - '0');
        }
        if (hasContentLength_ && contentLength_ != length) {
          finish(TransportOutcome::Kind::Malformed); return false;
        }
        contentLength_ = length;
        hasContentLength_ = true;
      } else if (equalAscii(line_, "Transfer-Encoding")) {
        // This minimal client accepts only a single final chunked coding.
        // No body is consumed; unsupported/ambiguous framing is never an ACK.
        if (hasTransferEncoding_ || !equalAscii(value, "chunked")) {
          finish(TransportOutcome::Kind::Malformed); return false;
        }
        hasTransferEncoding_ = true;
      }
    }
    lineLen_ = 0;
  } else {
    if ((ch < 32 && ch != '\t') || ch == 127 || lineLen_ >= sizeof line_ - 1) {
      finish(TransportOutcome::Kind::Malformed); return false;
    }
    line_[lineLen_++] = static_cast<char>(ch);
  }
  return true;
}

TransportPhase WifiHttpsTransport::step(uint32_t now, TransportOutcome &out) {
  if (!active_) { if (phase_ == TransportPhase::Done) out = result_; return phase_; }
  if (phase_ != TransportPhase::Associating && !io_.connected()) {
    finish(TransportOutcome::Kind::ConnectionFailed);
  } else if (phase_ == TransportPhase::Associating) {
    if (io_.connected()) {
      if (io_.clockReady(settings_.ntpServer)) phase_ = TransportPhase::Resolving;
    } else service(now);
  } else if (phase_ == TransportPhase::Resolving) {
    const WifiIo::Progress p = io_.resolve(host_, address_, sizeof address_);
    if (p == WifiIo::Progress::Failed) finish(TransportOutcome::Kind::ConnectionFailed);
    else if (p == WifiIo::Progress::Ready) phase_ = TransportPhase::Connecting;
  } else if (phase_ == TransportPhase::Connecting || phase_ == TransportPhase::Tls) {
    bool handshaking = false;
    const WifiIo::Progress p = io_.connectTls(address_, port_, host_, settings_.caPem, handshaking);
    if (p == WifiIo::Progress::Failed) finish(TransportOutcome::Kind::ConnectionFailed);
    else if (p == WifiIo::Progress::Ready) phase_ = TransportPhase::Sending;
    else if (handshaking) phase_ = TransportPhase::Tls;
  } else if (phase_ == TransportPhase::Sending) {
    const size_t left = requestLen_ - sent_;
    const size_t chunk = left < 512 ? left : 512;
    const int n = io_.write(reinterpret_cast<const uint8_t *>(request_) + sent_, chunk);
    if (n < 0 || static_cast<size_t>(n) > chunk) finish(TransportOutcome::Kind::ConnectionFailed);
    else {
      sent_ += static_cast<size_t>(n);
      if (sent_ == requestLen_) phase_ = TransportPhase::AwaitingResponse;
    }
  } else if (phase_ == TransportPhase::AwaitingResponse) {
    uint8_t bytes[128];
    const int n = io_.read(bytes, sizeof bytes);
    if (n < 0 || n > static_cast<int>(sizeof bytes)) finish(TransportOutcome::Kind::NoResponse);
    else for (int i = 0; i < n; ++i) if (!responseByte(bytes[i])) break;
  }
  if (phase_ == TransportPhase::Done) out = result_;
  return phase_;
}
}  // namespace bbcore
