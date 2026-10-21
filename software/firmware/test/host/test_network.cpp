// Host tests for the BardBox Network owner with a scripted fake transport.
// Built with a placeholder endpoint (never contacted), recording every 5 s and
// short deadlines. Simulated transport, storage and time only: no network,
// Wi-Fi, TLS or hardware evidence.
#define FIRMWARE_VERSION "2.0.0-new"
#include <BardBoxInterface.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "FakeStorage.h"
#include "FakeTransport.h"

uint32_t g_host_millis = 0;

static int failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++failures;                                                                \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                            \
  } while (0)

using bbcore::NetworkState;
using Plan = FakeTransport::Plan;
using Mode = FakeTransport::Mode;
using Phase = bbcore::TransportPhase;

static std::string ask(BardBoxInterface &box, HostStream &io, const char *cmd) {
  io.output.clear();
  io.input.append(cmd).append("\n");
  box.runServiceStep();
  std::string out = io.output;
  if (!out.empty() && out[out.size() - 1] == '\n') out.erase(out.size() - 1);
  return out;
}

static std::string field(const std::string &line, const char *key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = line.find(k);
  if (at == std::string::npos) return "";
  const size_t end = line.find(' ', at + k.size());
  return line.substr(at + k.size(), end == std::string::npos ? std::string::npos : end - at - k.size());
}

// Both owners, interleaved every 10 ms, as the two device tasks would run.
static void run(BardBoxInterface &box, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    g_host_millis += 10;
    box.runServiceStep();
    box.runNetworkStep();
  }
}

// Steps until the predicate holds or the limit passes; returns elapsed ms.
template <typename Pred>
static uint32_t runUntil(BardBoxInterface &box, Pred pred, uint32_t limitMs) {
  uint32_t t = 0;
  while (!pred() && t < limitMs) {
    run(box, 10);
    t += 10;
  }
  return t;
}

static std::string queueSeq(const std::string &body) {
  const size_t at = body.find("\"seq\":");
  return at == std::string::npos ? "" : body.substr(at + 6, body.find('}', at) - at - 6);
}

static bool setup(BardBoxInterface &box, HostStream &io, FakeStorage &storage, FakeTransport *transport,
                  const char *fw = FIRMWARE_VERSION) {
  BardBoxSensor bme = box.addSensor("bme280");
  bme.addMeasurement("humidity", "%");
  bme.addMeasurement("pressure", "Pa");
  box.useStorage(&storage);
  if (transport) box.useTransport(transport);
  return box.beginWithPort(io, fw, false);
}

static void test_explicit_states() {
  bbcore::DeliveryMailbox mailbox;
  mailbox.begin();
  FakeTransport t;
  bbcore::NetworkSettings s;
  bbcore::BardBoxNetwork net;

  s.enabled = false;
  net.begin(s, &t, 0, mailbox);
  CHECK(net.state() == NetworkState::Disabled && !net.active());
  s.enabled = true;
  s.endpointConfigured = false;
  net.begin(s, &t, 0, mailbox);
  CHECK(net.state() == NetworkState::Unconfigured && !net.active());
  s.endpointConfigured = true;
  net.begin(s, nullptr, 0, mailbox);
  CHECK(net.state() == NetworkState::NoTransport && !net.active());
  net.step(100000, mailbox);
  bbcore::DeliveryRequest r;
  CHECK(!mailbox.takeRequest(r));  // an inactive network asks for nothing
  CHECK(strcmp(bbcore::networkStateName(mailbox.stats().state), "no_transport") == 0);
}

static void test_handoff_is_bounded_and_owner_side_only() {
  bbcore::DeliveryMailbox mailbox;
  mailbox.begin();
  FakeTransport t;
  bbcore::BardBoxNetwork net;
  net.begin(bbcore::NetworkSettings(), &t, 0, mailbox);
  for (uint32_t now = 0; now < 5000; now += 10) net.step(now, mailbox);
  // Without the Buffer owner answering, the network only waits: one request,
  // no transport attempt, and no second request.
  CHECK(net.state() == NetworkState::WaitingRecord);
  CHECK(t.bodies.empty());
  bbcore::DeliveryRequest extra;
  extra.kind = bbcore::DeliveryRequest::Kind::Oldest;
  CHECK(!mailbox.post(extra));
  bbcore::DeliveryRequest got;
  CHECK(mailbox.takeRequest(got) && got.kind == bbcore::DeliveryRequest::Kind::Oldest);
  CHECK(!mailbox.takeRequest(got));
}

static void test_backoff_is_capped() {
  bbcore::BardBoxNetwork net;
  const uint32_t base = BARDBOX_NETWORK_RETRY_BASE_MS, cap = BARDBOX_NETWORK_RETRY_MAX_MS;
  CHECK(net.backoffFor(1) == base);
  CHECK(net.backoffFor(2) == base * 2);
  CHECK(net.backoffFor(3) == base * 4);
  CHECK(net.backoffFor(4) == cap);
  CHECK(net.backoffFor(1000000) == cap);
}

static uint32_t acked(BardBoxInterface &box) { return box.networkStats().acked; }

// Every non-acceptance keeps the same head; only the final 2xx removes it.
static void test_failures_retain_head_then_accept() {
  FakeStorage storage;
  FakeTransport t;
  t.plans = {Plan(Mode::Stall, 0, Phase::Associating), Plan(Mode::Stall, 0, Phase::Resolving),
             Plan(Mode::Stall, 0, Phase::Connecting),  Plan(Mode::Stall, 0, Phase::Tls),
             Plan(Mode::Stall, 0, Phase::Sending),     Plan(Mode::Stall, 0, Phase::AwaitingResponse),
             Plan(Mode::Respond, 302),                 Plan(Mode::Respond, 404),
             Plan(Mode::Respond, 503),                 Plan(Mode::Respond, 199),
             Plan(Mode::Respond, 300),                 Plan(Mode::Malformed),
             Plan(Mode::ConnectionFailed),             Plan(Mode::NoResponse),
             Plan(Mode::OtherAttempt, 200),            Plan(Mode::Unverified, 200),
             Plan(Mode::StartFails),                   Plan(Mode::Respond, 201)};
  const size_t plans = t.plans.size();
  t.fallback = Plan(Mode::Stall, 0, Phase::Associating);  // later records wait
  BardBoxInterface box;
  HostStream io;
  g_host_millis = 1000;
  CHECK(setup(box, io, storage, &t));
  runUntil(box, [&] { return acked(box) >= 1; }, 120000);
  CHECK(t.bodies.size() == plans);
  const std::string head = t.bodies.empty() ? "" : queueSeq(t.bodies[0]);
  for (size_t i = 0; i < t.bodies.size(); ++i) CHECK(t.bodies[i] == t.bodies[0]);  // same copied record
  CHECK(t.aborts == 6);
  const std::string info = ask(box, io, "INFO");
  CHECK(field(info, "net_acked") == "1");
  CHECK(field(info, "net_timeouts") == "6");
  CHECK(field(info, "net_failed") == "17");
  CHECK(field(info, "net_last") == "acked");
  printf("failures then accept: head seq=%s attempts=%s timeouts=%s failed=%s acked=%s stored=%s\n",
         head.c_str(), field(info, "net_attempts").c_str(), field(info, "net_timeouts").c_str(),
         field(info, "net_failed").c_str(), field(info, "net_acked").c_str(), field(info, "stored").c_str());
  printf("  transport log:");
  for (size_t i = 0; i < t.log.size(); ++i) printf(" %s", t.log[i].c_str());
  printf("\n");
  // The accepted record is gone; the head is now a later record.
  bbcore::Record r;
  bbcore::QueueToken token;
  if (box.bufferForDelivery().peekOldest(r, token)) CHECK(std::to_string(token.seq) != head);
}

// Each failure kind is reported by name; nothing is removed.
static void test_failure_names() {
  struct Case {
    Plan plan;
    const char *expect;
  } cases[] = {
      {Plan(Mode::Stall, 0, Phase::Associating), "timeout_association"},
      {Plan(Mode::Stall, 0, Phase::Resolving), "timeout_dns"},
      {Plan(Mode::Stall, 0, Phase::Connecting), "timeout_connect"},
      {Plan(Mode::Stall, 0, Phase::Tls), "timeout_tls"},
      {Plan(Mode::Stall, 0, Phase::Sending), "timeout_request"},
      {Plan(Mode::Stall, 0, Phase::AwaitingResponse), "timeout_response"},
      {Plan(Mode::Respond, 302), "http_302"},
      {Plan(Mode::Respond, 404), "http_404"},
      {Plan(Mode::Respond, 500), "http_500"},
      {Plan(Mode::Malformed), "malformed"},
      {Plan(Mode::ConnectionFailed), "connection_failed"},
      {Plan(Mode::NoResponse), "no_response"},
      {Plan(Mode::OtherAttempt, 200), "other_attempt"},
      {Plan(Mode::Unverified, 204), "unverified"},
      {Plan(Mode::StartFails), "start_failed"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    FakeStorage storage;
    FakeTransport t;
    t.fallback = cases[i].plan;
    BardBoxInterface box;
    HostStream io;
    g_host_millis = 1000;
    CHECK(setup(box, io, storage, &t));
    runUntil(box, [&] { return box.networkStats().failed >= 1; }, 30000);
    const bbcore::NetworkStats n = box.networkStats();
    if (strcmp(n.lastOutcome, cases[i].expect) != 0) {
      ++failures;
      fprintf(stderr, "case %s: got %s\n", cases[i].expect, n.lastOutcome);
    }
    CHECK(n.acked == 0 && n.state == NetworkState::Backoff && n.retryInMs == BARDBOX_NETWORK_RETRY_BASE_MS);
    if (strcmp(cases[i].expect, "connection_failed") == 0)
      CHECK(strcmp(n.lastError, "unknown") == 0 && field(ask(box, io, "INFO"), "net_error") == "unknown");
    CHECK(box.bufferForDelivery().status().stored >= 1);
  }
}

// A 2xx whose removal fails keeps the head; it is delivered again (same
// queue token) and removed on the next accepted attempt.
static void test_failed_removal_retains_head() {
  FakeStorage storage;
  FakeTransport t;
  BardBoxInterface box;
  HostStream io;
  g_host_millis = 1000;
  CHECK(setup(box, io, storage, &t));
  runUntil(box, [&] { return box.bufferForDelivery().status().stored >= 1; }, 6000);
  storage.failRemoves = 1;
  runUntil(box, [&] { return box.networkStats().failed >= 1; }, 2000);
  CHECK(strcmp(box.networkStats().lastOutcome, "remove_failed") == 0);
  CHECK(box.bufferForDelivery().status().stored == 1);
  runUntil(box, [&] { return acked(box) >= 1; }, 5000);
  CHECK(t.bodies.size() == 2 && t.bodies[0] == t.bodies[1]);
  CHECK(box.bufferForDelivery().status().stored == 0);
  printf("failed removal: attempts=%u bodies_identical=%d stored_after=%lu\n", (unsigned)t.bodies.size(),
         t.bodies.size() == 2 && t.bodies[0] == t.bodies[1],
         (unsigned long)box.bufferForDelivery().status().stored);
}

// Recording and Serial continue while attempts stall; once the transport
// recovers, the backlog drains oldest first, each record exactly once.
static void test_stall_then_fifo_drain(const char *&sampleOut) {
  static std::string sample;
  FakeStorage storage;
  FakeTransport t;
  t.fallback = Plan(Mode::Stall, 0, Phase::Connecting);
  BardBoxInterface box;
  HostStream io;
  g_host_millis = 1000;
  CHECK(setup(box, io, storage, &t));
  BardBoxSensor h;  // values come from the registered sensor via publish below
  uint32_t lastRecord = 0;
  bool serialOk = true;
  for (int i = 0; i < 6; ++i) {
    run(box, 5000);
    const std::string info = ask(box, io, "INFO");  // answered within one step
    serialOk = serialOk && info.compare(0, 8, "OK INFO ") == 0;
    const uint32_t rec = static_cast<uint32_t>(atoi(field(info, "record").c_str()));
    CHECK(rec > lastRecord);
    lastRecord = rec;
  }
  CHECK(serialOk);
  const uint32_t backlog = box.bufferForDelivery().status().stored;
  CHECK(backlog >= 5);
  CHECK(acked(box) == 0 && box.networkStats().timeouts >= 3);
  t.fallback = Plan(Mode::Respond, 200);
  t.bodies.clear();
  runUntil(box, [&] { return acked(box) >= backlog; }, 30000);
  CHECK(acked(box) >= backlog);
  int prev = 0;
  bool ordered = true;
  for (size_t i = 0; i < t.bodies.size(); ++i) {
    const int seq = atoi(queueSeq(t.bodies[i]).c_str());
    ordered = ordered && seq > prev;
    prev = seq;
  }
  CHECK(ordered);
  CHECK(t.bodies.size() >= backlog);
  printf("stall then drain: records_during_stall=%lu backlog=%lu delivered=%u fifo_once=%d serial_ok=%d\n",
         (unsigned long)lastRecord, (unsigned long)backlog, (unsigned)t.bodies.size(), ordered, serialOk);
  if (!t.bodies.empty()) sample = t.bodies[0];
  sampleOut = sample.c_str();
}

// A record saved by an earlier firmware is delivered with its own
// attribution, not the running firmware's.
static void test_original_attribution() {
  FakeStorage storage;
  g_host_millis = 1000;
  {
    BardBoxInterface old;
    HostStream io;
    CHECK(setup(old, io, storage, nullptr, "1.0.0-old"));
    CHECK(old.networkStats().state == NetworkState::NoTransport);
    run(old, 11000);
    CHECK(old.bufferForDelivery().status().stored == 2);
  }
  FakeTransport t;
  BardBoxInterface box;
  HostStream io;
  box.setHostBootId(0x5eed0002u);
  CHECK(setup(box, io, storage, &t));
  runUntil(box, [&] { return acked(box) >= 2; }, 5000);
  CHECK(t.bodies.size() >= 2);
  for (size_t i = 0; i < 2 && i < t.bodies.size(); ++i) {
    CHECK(t.bodies[i].find("\"firmware\":\"1.0.0-old\"") != std::string::npos);
    CHECK(t.bodies[i].find("\"boot\":\"5eed0001\"") != std::string::npos);
    CHECK(t.bodies[i].find("2.0.0-new") == std::string::npos);
  }
  run(box, 6000);
  CHECK(t.bodies.size() >= 3 && t.bodies[2].find("\"firmware\":\"2.0.0-new\"") != std::string::npos);
  printf("original attribution: first two bodies fw=1.0.0-old boot=5eed0001; third fw=2.0.0-new\n");
}

// The longest names and units registration accepts, all escaped, fit.
static void test_worst_case_body_fits() {
  static bbcore::Record r;
  r.bootId = 0xffffffffu;
  r.seq = r.dueMs = r.capturedMs = 0xffffffffu;
  memset(r.attribution.uid, 'u', 32);
  memset(r.attribution.firmware, 'f', 32);
  memset(r.attribution.protocol, 'p', 16);
  r.channelCount = bbcore::kMaxChannels;
  for (uint8_t c = 0; c < r.channelCount; ++c) {
    memset(r.schema[c].sensor, 's', 23);
    memset(r.schema[c].name, 'n', 23);
    memset(r.schema[c].unit, '"', 15);
    r.channels[c].value.status = bbcore::ChannelStatus::Valid;
    r.channels[c].value.f = -1.2345678901234567e-300;
    r.channels[c].publishSeq = r.channels[c].handoffAgeMs = 0xffffffffu;
  }
  bbcore::QueueToken token;
  token.generation = token.seq = 0xffffffffu;
  static char body[bbcore::kMaxDeliveryBody];
  const size_t n = bbcore::serializeDeliveryCandidate(r, token, body, sizeof body);
  CHECK(n > 0 && n < bbcore::kMaxDeliveryBody);
  CHECK(bbcore::serializeDeliveryCandidate(r, token, body, n) == 0);  // too small is refused, not cut
  bbcore::ConnectivityStatus link;  // worst-case device_status as well
  link.wifi = bbcore::WifiState::Disconnected;
  snprintf(link.ip, sizeof link.ip, "%s", "255.255.255.255");
  snprintf(link.mac, sizeof link.mac, "%s", "ff:ff:ff:ff:ff:ff");
  link.rssiKnown = true;
  link.rssiDbm = -127;
  memset(link.ssid, 0x01, 32);  // control bytes are escaped as \u0001
  memset(link.receiver, 'r', sizeof link.receiver - 1);
  const size_t withStatus = bbcore::serializeDeliveryCandidate(r, token, body, sizeof body, &link);
  CHECK(withStatus > n && withStatus < bbcore::kMaxDeliveryBody);
  printf("worst-case candidate body with device_status: %u of %u bytes\n", (unsigned)withStatus,
         (unsigned)bbcore::kMaxDeliveryBody);
  printf("worst-case candidate body: %u of %u bytes\n", (unsigned)n, (unsigned)bbcore::kMaxDeliveryBody);
}

// A delayed task must not accept a late response or reset an expired phase
// simply because its next transport step reports progress.
static void test_expired_attempt_cannot_ack_or_advance() {
  for (int response = 0; response < 2; ++response) {
    bbcore::DeliveryMailbox mailbox;
    mailbox.begin();
    FakeTransport transport;
    bbcore::BardBoxNetwork network;
    network.begin(bbcore::NetworkSettings(), &transport, 0, mailbox);
    network.step(0, mailbox);
    bbcore::DeliveryRequest request;
    CHECK(mailbox.takeRequest(request));
    bbcore::DeliveryResult result;
    result.kind = bbcore::DeliveryResult::Kind::Record;
    result.id = request.id;
    result.bodyLen = 2;
    memcpy(result.body, "{}", 2);
    mailbox.postResult(result);
    network.step(1, mailbox);
    if (response) {
      for (uint32_t now = 2; now <= 6; ++now) network.step(now, mailbox);
    }
    network.step(2000, mailbox);
    CHECK(network.state() == NetworkState::Backoff);
    CHECK(mailbox.stats().timeouts == 1);
    CHECK(transport.aborts == 1);
    CHECK(!mailbox.takeRequest(request));  // never posts a late ACK
  }
}

// Wi-Fi diagnostics (template#36): INFO and the delivery body's device_status
// carry the same names and meanings; IP and RSSI are unavailable unless
// connected; nothing secret appears.
static std::string jsonField(const std::string &body, const char *key) {
  const std::string k = std::string("\"") + key + "\":";
  const size_t at = body.find(k, body.find("\"device_status\""));
  if (at == std::string::npos) return "<missing>";
  size_t v = at + k.size(), end;
  if (body[v] == '"') return body.substr(v + 1, body.find('"', v + 1) - v - 1);
  end = body.find_first_of(",}", v);
  return body.substr(v, end - v);
}
static std::string unhex(const std::string &h) {
  if (h == "-") return "";
  std::string out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) out += static_cast<char>(strtol(h.substr(i, 2).c_str(), nullptr, 16));
  return out;
}

static void test_wifi_diagnostics_in_info_and_payload() {
  FakeStorage storage;
  FakeTransport t;
  BardBoxInterface box;
  HostStream io;
  g_host_millis = 1000;
  CHECK(setup(box, io, storage, &t));
  run(box, 1500);
  std::string info = ask(box, io, "INFO");
  CHECK(field(info, "wifi") == "connected" && field(info, "ip") == "192.0.2.10");
  CHECK(field(info, "mac") == "02:00:5e:10:20:30" && field(info, "rssi_dbm") == "-61");
  CHECK(unhex(field(info, "wifi_ssid")) == "BardWireless" && field(info, "receiver") == "https://receiver.invalid");
  runUntil(box, [&] { return !t.bodies.empty(); }, 8000);
  CHECK(!t.bodies.empty());
  const std::string body = t.bodies.empty() ? "" : t.bodies[0];
  // Same fields, same meanings, in both places (encoding aside).
  CHECK(jsonField(body, "at") == "send");
  CHECK(jsonField(body, "wifi") == field(info, "wifi") && jsonField(body, "ip") == field(info, "ip"));
  CHECK(jsonField(body, "mac") == field(info, "mac") && jsonField(body, "rssi_dbm") == field(info, "rssi_dbm"));
  CHECK(jsonField(body, "wifi_ssid") == unhex(field(info, "wifi_ssid")) && jsonField(body, "receiver") == field(info, "receiver"));
  CHECK(body.find("\"uid\":\"unassigned\"") != std::string::npos);  // the record itself is unchanged

  // Disconnected: IP and RSSI unavailable, never the last value; MAC kept.
  t.linkUp = false;
  t.fallback = Plan(Mode::Stall, 0, Phase::Associating);
  run(box, 1100);
  info = ask(box, io, "INFO");
  CHECK(field(info, "wifi") == "disconnected" && field(info, "ip") == "-" && field(info, "rssi_dbm") == "-");
  CHECK(field(info, "mac") == "02:00:5e:10:20:30");
  printf("wifi diagnostics: connected and disconnected INFO/device_status agree\n  %s\n",
         info.substr(info.find(" wifi=")).c_str());
}

// The longest INFO the firmware can produce fits the bounded reply buffer.
static void test_info_worst_case_fits() {
  bbcore::SerialStatus st;
  st.uid = st.firmware = "ffffffffffffffffffffffffffffffff";
  st.bootId = st.recordingIntervalS = st.stored = st.capacity = st.evicted = st.pendingRam = 0xffffffffu;
  st.lostRam = st.writeFailures = st.interrupted = st.quarantined = st.skippedBoundaries = 0xffffffffu;
  st.hidden = st.unreadable = st.metaRebuilt = st.netAttempts = st.netAcked = st.netFailed = 0xffffffffu;
  st.netTimeouts = st.netRetryMs = 0xffffffffu;
  st.storage = "unavailable";
  st.queueState = "sequence_exhausted";
  st.network = "waiting_record";
  st.registrationErrors = 255;
  snprintf(st.netLast, sizeof st.netLast, "%s", "timeout_association");
  st.link.wifi = bbcore::WifiState::Disconnected;
  snprintf(st.link.ip, sizeof st.link.ip, "%s", "255.255.255.255");
  snprintf(st.link.mac, sizeof st.link.mac, "%s", "ff:ff:ff:ff:ff:ff");
  st.link.rssiKnown = true;
  st.link.rssiDbm = -127;
  memset(st.link.ssid, '"', 32);
  memset(st.link.receiver, 'r', sizeof st.link.receiver - 1);
  snprintf(st.netError, sizeof st.netError, "%s", "tls:-0x7fffffff:0xffffffff");
  struct Source : bbcore::SerialDataSource {
    bbcore::SerialStatus s;
    void status(bbcore::SerialStatus &out) override { out = s; }
    uint8_t sensorCount() override { return bbcore::kMaxSensors; }
    const char *sensorName(uint8_t) override { return "sssssssssssssssssssssss"; }
    uint8_t channelCount() override { return 0; }
    const char *channelName(uint8_t) override { return ""; }
    uint8_t channelSensor(uint8_t) override { return 0; }
    bool latestRecord(bbcore::Record &r) override { r.seq = r.capturedMs = 0xffffffffu; return true; }
    uint8_t liveChannels(bbcore::RecordChannel *, uint32_t &) override { return 0; }
  } source;
  source.s = st;
  static bbcore::BardBoxSerial serial;
  HostStream io;
  serial.begin(io, source);
  io.input = "INFO\n";
  serial.service(1);
  CHECK(io.output.compare(0, 8, "OK INFO ") == 0 && io.output.find(" receiver=rrr") != std::string::npos);
  printf("worst-case INFO: %u of %u bytes\n", (unsigned)io.output.size(), (unsigned)bbcore::BardBoxSerial::kMaxReply);
}

int main(int argc, char **argv) {
  test_expired_attempt_cannot_ack_or_advance();
  test_explicit_states();
  test_handoff_is_bounded_and_owner_side_only();
  test_backoff_is_capped();
  test_failure_names();
  test_failures_retain_head_then_accept();
  test_failed_removal_retains_head();
  const char *sample = "";
  test_stall_then_fifo_drain(sample);
  test_original_attribution();
  test_worst_case_body_fits();
  test_wifi_diagnostics_in_info_and_payload();
  test_info_worst_case_fits();
  if (argc > 1) {  // sample body for an external JSON check
    FILE *f = fopen(argv[1], "w");
    if (f) {
      fputs(sample, f);
      fclose(f);
    }
  }
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox network tests: all checks passed (simulated transport, storage and time)\n");
  return 0;
}
