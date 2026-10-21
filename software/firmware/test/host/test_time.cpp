// Host tests for BardBoxTime and record UTC timestamps (template#49). Built
// twice: record timestamps on (default) and off. Simulated clock, storage
// and transport only; not NTP, RTC or hardware evidence.
#define FIRMWARE_VERSION "0.0.0-time-test"
#include <BardBoxInterface.h>
#include <BardBoxRecordCodec.h>

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

static const bool kOn = BARDBOX_TIME_RECORD_TIMESTAMPS != 0;

// A controllable synchronization provider standing in for SNTP.
struct FakeTime : bbcore::TimeSyncProvider {
  int starts = 0;
  uint32_t syncs = 0, lastSync = 0;
  int64_t utcAtZero = 1792540800000LL;  // 2026-10-21T00:00:00.000Z at host millis 0
  void start(const char *, uint32_t) override { ++starts; }
  bool utcNow(int64_t &ms) override {
    if (!syncs) return false;
    ms = utcAtZero + g_host_millis;
    return true;
  }
  uint32_t syncCount() override { return syncs; }
  uint32_t lastSyncMonotonicMs() override { return lastSync; }
  void sync() { ++syncs; lastSync = g_host_millis; }
};

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
  if (at == std::string::npos) return "<missing>";
  const size_t end = line.find(' ', at + k.size());
  return line.substr(at + k.size(), end == std::string::npos ? std::string::npos : end - at - k.size());
}
static std::string json(const std::string &body, const char *key, size_t from = 0) {
  const std::string k = std::string("\"") + key + "\":";
  const size_t at = body.find(k, from);
  if (at == std::string::npos) return "<missing>";
  const size_t v = at + k.size();
  if (body[v] == '"') return body.substr(v + 1, body.find('"', v + 1) - v - 1);
  return body.substr(v, body.find_first_of(",}", v) - v);
}
static std::string status(const std::string &body, const char *key) {
  return json(body, key, body.find("\"device_status\""));
}
static void run(BardBoxInterface &box, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    g_host_millis += 10;
    box.runServiceStep();
    box.runNetworkStep();
  }
}

static void test_format_utc() {
  char b[25];
  CHECK(bbcore::formatUtc(0, b, sizeof b) && strcmp(b, "1970-01-01T00:00:00.000Z") == 0);
  CHECK(bbcore::formatUtc(1704067200123LL, b, sizeof b) && strcmp(b, "2024-01-01T00:00:00.123Z") == 0);
  CHECK(bbcore::formatUtc(951782400000LL, b, sizeof b) && strcmp(b, "2000-02-29T00:00:00.000Z") == 0);
  CHECK(bbcore::formatUtc(1709251199999LL, b, sizeof b) && strcmp(b, "2024-02-29T23:59:59.999Z") == 0);
  CHECK(!bbcore::formatUtc(-1, b, sizeof b) && !bbcore::formatUtc(0, b, 24));
}

// Unsynced records stay unsynced; synced ones get UTC of creation, which
// never changes across retries, restarts or later synchronizations. With
// record timestamps off, records say "disabled" while the clock still syncs.
static void test_record_timestamps() {
  FakeStorage storage;
  FakeTime clock;
  FakeTransport t;
  t.linkUp = false;  // no Wi-Fi yet: NTP must not start
  t.fallback = FakeTransport::Plan(FakeTransport::Mode::Stall, 0, bbcore::TransportPhase::Associating);
  std::string unsyncedBody, syncedBody;
  {
    BardBoxInterface box;
    HostStream io;
    BardBoxSensor demo = box.addSensor("demo");
    demo.addMeasurement("counter", "1", MeasurementType::Integer);
    box.useStorage(&storage);
    box.useTransport(&t);
    box.useTimeProvider(&clock);
    g_host_millis = 1000;
    CHECK(box.beginWithPort(io, FIRMWARE_VERSION, false));
    std::string info = ask(box, io, "INFO");
    CHECK(field(info, "time") == "unsynced" && field(info, "utc") == "unknown");
    CHECK(field(info, "record_utc") == (kOn ? "on" : "off"));
    demo.publish(1);
    run(box, 6000);  // record 1 at 5 s, before any sync
    CHECK(clock.starts == 0);
    t.linkUp = true;
    run(box, 1500);  // Wi-Fi up: NTP starts once
    CHECK(clock.starts == 1);
    clock.sync();
    demo.publish(2);
    run(box, 5000);  // record 2 after sync
    info = ask(box, io, "INFO");
    CHECK(field(info, "time") == "synced" && field(info, "utc").find("2026-10-21T00:00:") == 0);
    CHECK(clock.starts == 1);  // one owner, started once
  }
  {
    // Restart: saved records decode with the UTC status they were created with.
    BardBoxInterface box;
    HostStream io;
    BardBoxSensor demo = box.addSensor("demo");
    demo.addMeasurement("counter", "1", MeasurementType::Integer);
    box.useStorage(&storage);
    box.useTransport(&t);
    box.useTimeProvider(&clock);
    box.setHostBootId(0x5eed0002u);
    CHECK(box.beginWithPort(io, FIRMWARE_VERSION, false));
    t.fallback = FakeTransport::Plan(FakeTransport::Mode::Respond, 503);  // first attempt fails
    t.plans.clear();
    t.bodies.clear();  // attempts stalled in the first boot are not part of this check
    clock.sync();
    run(box, 300);
    t.fallback = FakeTransport::Plan(FakeTransport::Mode::Respond, 201);
    run(box, 3000);
    CHECK(t.bodies.size() >= 3);
    if (t.bodies.size() >= 3) {
      unsyncedBody = t.bodies[0];
      syncedBody = t.bodies.back();
      // The retried first record keeps its UTC status after the clock synced.
      CHECK(json(t.bodies[0], "recorded_utc") == json(t.bodies[1], "recorded_utc"));
      CHECK(json(unsyncedBody, "recorded_utc") == "null");
      CHECK(json(unsyncedBody, "recorded_utc_status") == (kOn ? "unsynced" : "disabled"));
      CHECK(json(syncedBody, "recorded_utc_status") == (kOn ? "ntp" : "disabled"));
      if (kOn) CHECK(json(syncedBody, "recorded_utc") == "2026-10-21T00:00:11.000Z");
      else CHECK(json(syncedBody, "recorded_utc") == "null");
      CHECK(json(syncedBody, "acquired_time") == "unknown");  // never relabelled as acquisition
      // device_status and INFO agree on Time.
      const std::string info = ask(box, io, "INFO");
      CHECK(status(syncedBody, "time") == "synced" && field(info, "time") == "synced");
      CHECK(status(syncedBody, "record_utc") == field(info, "record_utc"));
      CHECK(status(syncedBody, "time_sync_age_s") != "null");
    }
  }
  printf("record timestamps %s: unsynced record %s, synced record %s\n", kOn ? "ON" : "OFF",
         json(unsyncedBody, "recorded_utc_status").c_str(),
         kOn ? json(syncedBody, "recorded_utc").c_str() : json(syncedBody, "recorded_utc_status").c_str());
}

// A record queued by earlier firmware (payload schema 2, no UTC) still decodes.
static void test_schema2_record_decodes() {
  static bbcore::Record r;
  r.bootId = 0x5eed0001u;
  r.seq = 7;
  r.capturedMs = 5000;
  r.queueGeneration = 9;
  r.queueSeq = 3;
  strcpy(r.attribution.uid, "bb-old-uid-001");
  strcpy(r.attribution.firmware, "0.0.0-wifi42");
  strcpy(r.attribution.protocol, "v2-candidate");
  r.channelCount = 1;
  strcpy(r.schema[0].sensor, "demo");
  strcpy(r.schema[0].name, "answer");
  strcpy(r.schema[0].unit, "1");
  r.channels[0].value.status = bbcore::ChannelStatus::Valid;
  r.channels[0].value.f = 42;
  uint8_t frame[bbcore::kMaxFrameBytes];
  const size_t n = bbcore::encodeRecordFrame(r, frame, sizeof frame);
  CHECK(n > 0);
  // Rewrite as schema 2: drop the 9 UTC bytes after capturedMs (offset 11 + 1 + 6*4).
  std::string old(reinterpret_cast<char *>(frame), n);
  old.erase(11 + 1 + 24, 9);
  old[11] = 2;
  const size_t payload = old.size() - 11;
  old[5] = static_cast<char>(payload & 0xff);
  old[6] = static_cast<char>(payload >> 8);
  const uint32_t crc = bbcore::crc32(reinterpret_cast<const uint8_t *>(old.data()) + 11, payload);
  for (int i = 0; i < 4; ++i) old[7 + i] = static_cast<char>((crc >> (8 * i)) & 0xff);
  static bbcore::Record out;
  CHECK(bbcore::decodeRecordFrame(reinterpret_cast<const uint8_t *>(old.data()), old.size(), out) ==
        bbcore::DecodeResult::Ok);
  CHECK(out.recordedUtc == bbcore::RecordedUtc::NotRecorded && out.channels[0].value.f == 42 &&
        strcmp(out.attribution.uid, "bb-old-uid-001") == 0 && out.queueSeq == 3);
  char body[bbcore::kMaxDeliveryBody];
  bbcore::QueueToken token;
  token.generation = 9;
  token.seq = 3;
  const size_t len = bbcore::serializeDeliveryCandidate(out, token, body, sizeof body);
  CHECK(len > 0 && json(std::string(body, len), "recorded_utc_status") == "not_recorded");
}

int main() {
  test_format_utc();
  test_record_timestamps();
  test_schema2_record_decodes();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox time tests (record timestamps %s): all checks passed (simulated clock, storage and transport)\n",
         kOn ? "ON" : "OFF");
  return 0;
}
