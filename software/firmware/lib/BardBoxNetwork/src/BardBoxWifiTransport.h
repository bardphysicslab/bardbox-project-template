#pragma once
#include "BardBoxPayload.h"
#include "BardBoxTransport.h"

namespace bbcore {
// I/O only, not retry/queue policy. Every operation advances without waiting.
// A provider owns its socket and DNS callback lifetime; no Buffer access.
class WifiIo {
 public:
  enum class Progress { Pending, Ready, Failed };
  virtual ~WifiIo() {}
  virtual bool reset() = 0;
  virtual bool associate(const char *ssid, const char *password) = 0;
  virtual bool connected() = 0;
  virtual bool clockReady(const char *ntpServer) = 0;
  virtual Progress resolve(const char *host, char *address, size_t capacity) = 0;
  virtual Progress connectTls(const char *address, uint16_t port,
                              const char *hostname, const char *caPem,
                              bool &handshaking) = 0;
  // Positive: bytes, 0: would block, negative: failed/closed.
  virtual int write(const uint8_t *data, size_t count) = 0;
  virtual int read(uint8_t *data, size_t capacity) = 0;
  virtual void close() = 0;
};

struct WifiSettings {
  const char *ssid = nullptr;
  const char *password = nullptr;  // nullptr: open Wi-Fi, never an example password
  const char *endpoint = nullptr;
  const char *caPem = nullptr;
  const char *bearer = nullptr;    // optional, set-only; never returned in INFO
  const char *ntpServer = "pool.ntp.org";  // CESH default, used only on device
  uint32_t reconnectMs = 30000;
};

// Real HTTPS adapter, with injected platform I/O for controlled tests.
// One owner calls start/step/abort. Settings strings must outlive this object.
// Request and response are bounded; no response body is retained or logged.
class WifiHttpsTransport : public Transport {
 public:
  WifiHttpsTransport(WifiIo &io, const WifiSettings &settings);
  const char *name() const override { return "esp32_wifi_https"; }
  bool configured() const { return configured_; }
  bool start(uint32_t attemptId, const uint8_t *body, size_t len, uint32_t nowMs) override;
  TransportPhase step(uint32_t nowMs, TransportOutcome &out) override;
  void abort() override;
  void service(uint32_t nowMs) override;
 private:
  bool parseEndpoint();
  bool responseByte(uint8_t ch);  // false: malformed, oversize, or complete
  void finish(TransportOutcome::Kind kind, int status = 0);
  WifiIo &io_;
  WifiSettings settings_;
  bool configured_ = false, active_ = false, haveAssociationTry_ = false;
  uint32_t lastAssociationTry_ = 0, attemptId_ = 0;
  TransportPhase phase_ = TransportPhase::Idle;
  TransportOutcome result_;
  char host_[128] = {}, target_[384] = {}, address_[64] = {};
  uint16_t port_ = 443;
  char request_[kMaxDeliveryBody + 1024] = {};
  size_t requestLen_ = 0, sent_ = 0, responseBytes_ = 0, lineLen_ = 0;
  char line_[512] = {};
  bool firstLine_ = true, previousCr_ = false;
  int responseStatus_ = 0;
};
}  // namespace bbcore
