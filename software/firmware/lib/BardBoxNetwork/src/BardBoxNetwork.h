#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxDelivery.h"
#include "BardBoxNetworkConfig.h"
#include "BardBoxTransport.h"

namespace bbcore {

struct NetworkSettings {
  bool enabled;
  bool endpointConfigured;
  uint32_t associationMs, dnsMs, connectMs, tlsMs, requestMs, responseMs;
  uint32_t retryBaseMs, retryMaxMs, idlePollMs;
  NetworkSettings()
      : enabled(BARDBOX_NETWORK_ENABLED != 0),
        endpointConfigured(BARDBOX_NETWORK_HAS_ENDPOINT != 0),
        associationMs(BARDBOX_NETWORK_ASSOCIATION_TIMEOUT_MS),
        dnsMs(BARDBOX_NETWORK_DNS_TIMEOUT_MS),
        connectMs(BARDBOX_NETWORK_CONNECT_TIMEOUT_MS),
        tlsMs(BARDBOX_NETWORK_TLS_TIMEOUT_MS),
        requestMs(BARDBOX_NETWORK_REQUEST_TIMEOUT_MS),
        responseMs(BARDBOX_NETWORK_RESPONSE_TIMEOUT_MS),
        retryBaseMs(BARDBOX_NETWORK_RETRY_BASE_MS),
        retryMaxMs(BARDBOX_NETWORK_RETRY_MAX_MS),
        idlePollMs(BARDBOX_NETWORK_IDLE_POLL_MS) {}
};

// The Network owner: delivers the oldest saved record through an injected
// transport and asks for its removal only after an accepted attempt. It never
// calls the Buffer: records and acknowledgements go through the mailbox.
//
// Acceptance: an HTTP 2xx outcome for the current attempt ID, reported by the
// transport as coming from the verified configured endpoint. Redirects, 4xx,
// 5xx, timeouts, connection failures, malformed or missing responses, an
// outcome for another attempt and an unverified endpoint are not acceptance:
// the record stays at the head and is retried after a capped backoff. A
// failed removal also retains and retries the head (the receiver may then see
// the same queue token twice).
class BardBoxNetwork {
 public:
  void begin(const NetworkSettings &settings, Transport *transport, uint32_t nowMs, DeliveryMailbox &mailbox);
  // Bounded step. Never blocks and never holds the mailbox lock while the
  // transport runs. Call from one task only.
  void step(uint32_t nowMs, DeliveryMailbox &mailbox);

  NetworkState state() const { return stats_.state; }
  // True when this build should run network steps (enabled, configured and a
  // transport present).
  bool active() const { return transport_ != nullptr && stats_.state != NetworkState::Disabled &&
                               stats_.state != NetworkState::Unconfigured &&
                               stats_.state != NetworkState::NoTransport; }
  uint32_t backoffFor(uint32_t consecutiveFailures) const;

 private:
  uint32_t deadlineFor(TransportPhase phase) const;
  void finish(uint32_t nowMs, const TransportOutcome &outcome, DeliveryMailbox &mailbox);
  void fail(uint32_t nowMs, const char *reason);
  void outcome(const char *reason);

  NetworkSettings settings_;
  Transport *transport_ = nullptr;
  NetworkStats stats_;
  uint32_t nextTryMs_ = 0;
  uint32_t requestId_ = 0;
  uint32_t attemptId_ = 0;
  uint32_t attemptStartMs_ = 0;
  uint32_t phaseStartMs_ = 0;
  TransportPhase phase_ = TransportPhase::Idle;
  QueueToken token_;
  DeliveryResult result_;  // copied out of the mailbox; the transport reads its own copy
};

}  // namespace bbcore
