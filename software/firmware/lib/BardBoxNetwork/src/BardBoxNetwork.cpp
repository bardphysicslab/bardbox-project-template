#include "BardBoxNetwork.h"

#include <stdio.h>
#include <string.h>

namespace bbcore {

const char *transportPhaseName(TransportPhase phase) {
  switch (phase) {
    case TransportPhase::Idle: return "idle";
    case TransportPhase::Associating: return "association";
    case TransportPhase::Resolving: return "dns";
    case TransportPhase::Connecting: return "connect";
    case TransportPhase::Tls: return "tls";
    case TransportPhase::Sending: return "request";
    case TransportPhase::AwaitingResponse: return "response";
    case TransportPhase::Done: return "done";
  }
  return "unknown";
}

static bool reached(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

void BardBoxNetwork::begin(const NetworkSettings &settings, Transport *transport, uint32_t nowMs,
                           DeliveryMailbox &mailbox) {
  settings_ = settings;
  transport_ = transport;
  stats_ = NetworkStats();
  if (!settings.enabled)
    stats_.state = NetworkState::Disabled;
  else if (!settings.endpointConfigured)
    stats_.state = NetworkState::Unconfigured;
  else if (!transport)
    stats_.state = NetworkState::NoTransport;
  else
    stats_.state = NetworkState::Idle;
  nextTryMs_ = nowMs;
  linkRefreshMs_ = nowMs;
  refreshLink();
  mailbox.publishStats(stats_);
}

// Diagnostics only; never changes delivery. IP and RSSI are cleared unless the
// transport reports a connected link, so no stale value is shown as current.
void BardBoxNetwork::refreshLink() {
  ConnectivityStatus c;
  const char *ssid = settings_.ssid ? settings_.ssid : "";
  strncpy(c.ssid, ssid, sizeof c.ssid - 1);
  if (stats_.state == NetworkState::Disabled) {
    c.wifi = WifiState::Disabled;
  } else if (!active()) {
    c.wifi = WifiState::Unconfigured;
  } else {
    c.wifi = WifiState::Disconnected;
    transport_->linkStatus(c);
  }
  if (c.wifi != WifiState::Connected) {
    c.ip[0] = 0;
    c.rssiKnown = false;
    c.rssiDbm = 0;
  }
  memcpy(c.netError, stats_.lastError, sizeof c.netError);
  c.ip[sizeof c.ip - 1] = c.mac[sizeof c.mac - 1] = c.receiver[sizeof c.receiver - 1] = 0;
  stats_.link = c;
}

uint32_t BardBoxNetwork::deadlineFor(TransportPhase phase) const {
  switch (phase) {
    case TransportPhase::Resolving: return settings_.dnsMs;
    case TransportPhase::Connecting: return settings_.connectMs;
    case TransportPhase::Tls: return settings_.tlsMs;
    case TransportPhase::Sending: return settings_.requestMs;
    case TransportPhase::AwaitingResponse: return settings_.responseMs;
    default: return settings_.associationMs;  // Idle after start counts as association
  }
}

uint32_t BardBoxNetwork::backoffFor(uint32_t consecutiveFailures) const {
  uint32_t delay = settings_.retryBaseMs;
  for (uint32_t i = 1; i < consecutiveFailures && delay < settings_.retryMaxMs; ++i)
    delay = delay > settings_.retryMaxMs / 2 ? settings_.retryMaxMs : delay * 2;
  return delay < settings_.retryMaxMs ? delay : settings_.retryMaxMs;
}

void BardBoxNetwork::outcome(const char *reason) {
  strncpy(stats_.lastOutcome, reason, sizeof stats_.lastOutcome - 1);
  stats_.lastOutcome[sizeof stats_.lastOutcome - 1] = 0;
}

void BardBoxNetwork::fail(uint32_t nowMs, const char *reason) {
  ++stats_.failed;
  ++stats_.consecutiveFailures;
  stats_.retryInMs = backoffFor(stats_.consecutiveFailures);
  outcome(reason);
  nextTryMs_ = nowMs + stats_.retryInMs;
  stats_.state = NetworkState::Backoff;
}

void BardBoxNetwork::finish(uint32_t nowMs, const TransportOutcome &o, DeliveryMailbox &mailbox) {
  if (o.attemptId != attemptId_) return fail(nowMs, "other_attempt");
  switch (o.kind) {
    case TransportOutcome::Kind::HttpStatus:
      stats_.lastHttp = o.httpStatus;
      if (!o.endpointVerified) return fail(nowMs, "unverified");
      if (o.httpStatus >= 200 && o.httpStatus <= 299) {
        DeliveryRequest ack;
        ack.kind = DeliveryRequest::Kind::Ack;
        ack.id = ++requestId_;
        ack.token = token_;  // exactly the record this attempt carried
        if (!mailbox.post(ack)) return fail(nowMs, "mailbox_busy");
        stats_.state = NetworkState::WaitingAck;
        return;
      }
      char reason[16];
      snprintf(reason, sizeof reason, "http_%d", o.httpStatus);
      return fail(nowMs, reason);
    case TransportOutcome::Kind::ConnectionFailed:
      snprintf(stats_.lastError, sizeof stats_.lastError, "%s", o.detail[0] ? o.detail : "unknown");
      memcpy(stats_.link.netError, stats_.lastError, sizeof stats_.link.netError);
      return fail(nowMs, "connection_failed");
    case TransportOutcome::Kind::Malformed: return fail(nowMs, "malformed");
    case TransportOutcome::Kind::NoResponse: return fail(nowMs, "no_response");
    default: return fail(nowMs, "no_outcome");
  }
}

void BardBoxNetwork::step(uint32_t nowMs, DeliveryMailbox &mailbox) {
  if (active()) transport_->service(nowMs);
  if (nowMs - linkRefreshMs_ >= 1000) {
    linkRefreshMs_ = nowMs;
    refreshLink();
  }
  switch (stats_.state) {
    case NetworkState::Idle:
    case NetworkState::Backoff:
      if (reached(nowMs, nextTryMs_)) {
        DeliveryRequest request;
        request.kind = DeliveryRequest::Kind::Oldest;
        request.id = ++requestId_;
        if (mailbox.post(request)) stats_.state = NetworkState::WaitingRecord;
      }
      break;

    case NetworkState::WaitingRecord:
      if (!mailbox.takeResult(result_)) break;
      if (result_.id != requestId_) {
        fail(nowMs, "stale_result");
      } else {
        if (result_.kind == DeliveryResult::Kind::Empty) {
          stats_.state = NetworkState::Idle;
          stats_.retryInMs = 0;
          nextTryMs_ = nowMs + settings_.idlePollMs;
        } else if (result_.kind != DeliveryResult::Kind::Record) {
          fail(nowMs, result_.kind == DeliveryResult::Kind::TooLarge ? "too_large" : "read_failed");
        } else {
          token_ = result_.token;
          ++attemptId_;
          ++stats_.attempts;
          attemptStartMs_ = phaseStartMs_ = nowMs;
          phase_ = TransportPhase::Idle;
          if (transport_->start(attemptId_, reinterpret_cast<const uint8_t *>(result_.body), result_.bodyLen, nowMs))
            stats_.state = NetworkState::Delivering;
          else
            fail(nowMs, "start_failed");
        }
      }
      break;

    case NetworkState::Delivering: {
      // Enforce the deadline of the phase we were waiting in BEFORE asking
      // for progress. A late Done/phase transition cannot revive an expired
      // attempt or acknowledge its record after a delayed task wake-up.
      const uint32_t total = settings_.associationMs + settings_.dnsMs + settings_.connectMs +
                             settings_.tlsMs + settings_.requestMs + settings_.responseMs;
      if (nowMs - phaseStartMs_ >= deadlineFor(phase_) || nowMs - attemptStartMs_ >= total) {
        transport_->abort();
        ++stats_.timeouts;
        char reason[24];
        snprintf(reason, sizeof reason, "timeout_%s", transportPhaseName(
            phase_ == TransportPhase::Idle ? TransportPhase::Associating : phase_));
        fail(nowMs, reason);
        break;
      }
      TransportOutcome got;
      const TransportPhase phase = transport_->step(nowMs, got);
      if (phase == TransportPhase::Done) {
        finish(nowMs, got, mailbox);
        break;
      }
      if (phase != phase_) {
        phase_ = phase;
        phaseStartMs_ = nowMs;
      }
      break;
    }

    case NetworkState::WaitingAck:
      if (!mailbox.takeResult(result_)) break;
      if (result_.id != requestId_) {
        fail(nowMs, "stale_result");
      } else {
        if (result_.ack == BardBoxBuffer::AckResult::Removed) {
          ++stats_.acked;
          stats_.consecutiveFailures = 0;
          stats_.retryInMs = 0;
          outcome("acked");
          stats_.state = NetworkState::Idle;
          nextTryMs_ = nowMs;  // catch up: next record right away
        } else if (result_.ack == BardBoxBuffer::AckResult::StorageError) {
          fail(nowMs, "remove_failed");  // record kept at the head; delivered again
        } else {
          // Stale or empty: nothing was removed; look at the queue again.
          outcome(result_.ack == BardBoxBuffer::AckResult::StaleToken ? "ack_stale" : "ack_empty");
          stats_.state = NetworkState::Idle;
          nextTryMs_ = nowMs;
        }
      }
      break;

    default:  // Disabled, Unconfigured, NoTransport: nothing to do
      break;
  }
  mailbox.publishStats(stats_);
}

}  // namespace bbcore
