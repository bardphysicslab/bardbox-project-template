#pragma once
// Scripted, in-memory transport for host tests and demos. It never opens a
// socket: every "network" outcome here is simulated.
#include <stdio.h>

#include <deque>
#include <string>
#include <vector>

#include "BardBoxTransport.h"

class FakeTransport : public bbcore::Transport {
 public:
  using Phase = bbcore::TransportPhase;
  enum class Mode { Respond, Stall, StartFails, ConnectionFailed, Malformed, NoResponse, OtherAttempt, Unverified };
  struct Plan {
    Mode mode;
    int http;
    Phase stallAt;
    Plan(Mode m = Mode::Respond, int status = 200, Phase at = Phase::Connecting) : mode(m), http(status), stallAt(at) {}
  };

  std::deque<Plan> plans;  // one per attempt; when empty, fallback is used
  Plan fallback;
  std::vector<std::string> bodies;  // body of every started attempt
  std::vector<std::string> log;     // one line per finished or aborted attempt
  int aborts = 0;

  // Simulated link diagnostics (linkStatus).
  bool linkUp = true;
  const char *linkIp = "192.0.2.10";
  const char *linkMac = "02:00:5e:10:20:30";
  int linkRssi = -61;
  const char *failDetail = "";  // TransportOutcome::detail for ConnectionFailed
  const char *linkReceiver = "https://receiver.invalid";

  const char *name() const override { return "fake"; }
  void linkStatus(bbcore::ConnectivityStatus &out) override {
    snprintf(out.receiver, sizeof out.receiver, "%s", linkReceiver);
    snprintf(out.mac, sizeof out.mac, "%s", linkMac);
    out.wifi = linkUp ? bbcore::WifiState::Connected : bbcore::WifiState::Disconnected;
    snprintf(out.ip, sizeof out.ip, "%s", linkUp ? linkIp : "");
    out.rssiKnown = linkUp;
    out.rssiDbm = static_cast<int16_t>(linkRssi);
  }

  bool start(uint32_t attemptId, const uint8_t *body, size_t len, uint32_t) override {
    current_ = plans.empty() ? fallback : plans.front();
    if (!plans.empty()) plans.pop_front();
    bodies.push_back(std::string(reinterpret_cast<const char *>(body), len));
    if (current_.mode == Mode::StartFails) {
      log.push_back("start_failed");
      return false;
    }
    attempt_ = attemptId;
    phase_ = Phase::Associating;
    active_ = true;
    return true;
  }

  Phase step(uint32_t, bbcore::TransportOutcome &out) override {
    if (!active_) return Phase::Idle;
    if (current_.mode == Mode::Stall && phase_ == current_.stallAt) return phase_;
    phase_ = static_cast<Phase>(static_cast<int>(phase_) + 1);
    if (phase_ != Phase::Done) return phase_;
    active_ = false;
    out.attemptId = attempt_;
    out.endpointVerified = true;
    out.kind = bbcore::TransportOutcome::Kind::HttpStatus;
    out.httpStatus = current_.http;
    switch (current_.mode) {
      case Mode::ConnectionFailed:
        out.kind = bbcore::TransportOutcome::Kind::ConnectionFailed;
        snprintf(out.detail, sizeof out.detail, "%s", failDetail);
        break;
      case Mode::Malformed: out.kind = bbcore::TransportOutcome::Kind::Malformed; break;
      case Mode::NoResponse: out.kind = bbcore::TransportOutcome::Kind::NoResponse; break;
      case Mode::OtherAttempt: out.attemptId = attempt_ - 1; break;  // a late answer to an older attempt
      case Mode::Unverified: out.endpointVerified = false; break;
      default: break;
    }
    log.push_back("done");
    return phase_;
  }

  void abort() override {
    ++aborts;
    log.push_back(std::string("aborted_in_") + bbcore::transportPhaseName(phase_));
    active_ = false;
  }

  bool active() const { return active_; }

 private:
  Plan current_;
  uint32_t attempt_ = 0;
  Phase phase_ = Phase::Idle;
  bool active_ = false;
};
