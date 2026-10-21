// Host tests for BardBox M1 (SIMULATED values; no hardware evidence).
#define FIRMWARE_VERSION "0.0.0-host-test"
#include <BardBoxInterface.h>

#include <stdio.h>

#include "FakeStorage.h"

#include <string>

uint32_t g_host_millis = 0;

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)
#define CHECK_EQ_STR(a, b)                                              \
  do {                                                                  \
    std::string _a = (a), _b = (b);                                     \
    if (_a != _b) {                                                     \
      ++failures;                                                       \
      fprintf(stderr, "%s:%d: expected\n  %s\ngot\n  %s\n", __FILE__, __LINE__, _b.c_str(), _a.c_str()); \
    }                                                                   \
  } while (0)

// Sends one command and returns the reply line without its newline.
static std::string ask(BardBoxInterface &box, HostStream &io, const char *cmd) {
  io.output.clear();
  io.input.append(cmd).append("\n");
  box.runServiceStep();
  std::string out = io.output;
  if (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

static void advance(BardBoxInterface &box, uint32_t toMs) {
  g_host_millis = toMs;
  box.runServiceStep();
}

struct Fixture {
  FakeStorage fs;
  BardBoxInterface box;
  HostStream io;
  BardBoxSensor bme;
  Fixture() {
    g_host_millis = 1000;
    box.useStorage(&fs);
    bme = box.addSensor("bme280");
    bme.addMeasurement("humidity", "%");
    bme.addMeasurement("temperature", "degC");
    bme.addMeasurement("pressure", "Pa");
    CHECK(box.beginWithPort(io, FIRMWARE_VERSION, false));
  }
};

static void test_registration() {
  BardBoxInterface box;
  HostStream io;
  BardBoxSensor a = box.addSensor("bme280");
  CHECK(a.valid());
  CHECK(!box.addSensor("bme280").valid());   // duplicate sensor
  CHECK(!box.addSensor("Bad Name").valid()); // invalid name
  CHECK(a.addMeasurement("humidity", "%"));
  CHECK(!a.addMeasurement("humidity", "%")); // duplicate measurement
  CHECK(a.addMeasurement("count", "1", MeasurementType::Integer));
  CHECK(a.addMeasurement("door", "", MeasurementType::Boolean));
  CHECK(box.registrationErrors() == 3);
  CHECK(!box.beginWithPort(io, FIRMWARE_VERSION, false));  // begins, reports errors
  CHECK(!box.addSensor("late").valid());                   // closed after begin
  CHECK(!BardBoxSensor().publish(1.0f).accepted);           // invalid handle
}

static void test_publish_checks() {
  Fixture f;
  PublishResult r = f.bme.publish(43.0f, 21.5f);  // wrong count
  CHECK(!r.accepted);
  CHECK_EQ_STR(r.reason, "wrong_count");
  CHECK_EQ_STR(ask(f.box, f.io, "READ_LIVE"), "LIVE,v2,5eed0001,1000|bme280,publish=none");

  r = f.bme.publish(43.0f, 21.5f, NAN);  // one non-finite channel
  CHECK(r.accepted && r.faultyChannels == 1);
  g_host_millis = 1250;
  CHECK_EQ_STR(ask(f.box, f.io, "READ_LIVE"),
               "LIVE,v2,5eed0001,1250|bme280,publish=1,handoff_age_ms=250,acquired=unknown,"
               "humidity=43,temperature=21.5,pressure=!non_finite");

  r = f.bme.publish(43, true, 100980.5);  // int->FLOAT ok; bool->FLOAT invalid
  CHECK(r.accepted && r.faultyChannels == 1);
  CHECK_EQ_STR(ask(f.box, f.io, "READ_LIVE"),
               "LIVE,v2,5eed0001,1250|bme280,publish=2,handoff_age_ms=0,acquired=unknown,"
               "humidity=43,temperature=!invalid_type,pressure=100980");
}

static void test_types() {
  using bbcore::ChannelStatus;
  CHECK(bbcore::convertValue(MeasurementType::Integer, bbcore::ValueArg(43.7f)).status ==
        ChannelStatus::InvalidType);  // never truncated
  CHECK(bbcore::convertValue(MeasurementType::Integer, bbcore::ValueArg(43)).i == 43);
  CHECK(bbcore::convertValue(MeasurementType::Integer, bbcore::ValueArg(UINT64_MAX)).status ==
        ChannelStatus::NotRepresentable);
  CHECK(bbcore::convertValue(MeasurementType::Boolean, bbcore::ValueArg(1)).status ==
        ChannelStatus::InvalidType);
  CHECK(bbcore::convertValue(MeasurementType::Boolean, bbcore::ValueArg(true)).b);
  CHECK(bbcore::convertValue(MeasurementType::Float, bbcore::ValueArg(INFINITY)).status ==
        ChannelStatus::NonFinite);
}

static void test_records_vs_live() {
  Fixture f;
  CHECK_EQ_STR(ask(f.box, f.io, "HEADER"),
               "HDR,v2,record,boot,captured_ms,bme280.humidity,bme280.temperature,bme280.pressure");
  CHECK_EQ_STR(ask(f.box, f.io, "READ"), "ERR SAMPLE_NOT_READY");

  g_host_millis = 30000;
  f.bme.publish(43.0f, 21.5f, 100980.0f);
  advance(f.box, 61000);  // first boundary was due at 61000
  CHECK_EQ_STR(ask(f.box, f.io, "READ"), "DAT,v2,1,5eed0001,61000,43,21.5,100980");

  g_host_millis = 62000;
  f.bme.publish(44.0f, 22.0f, NAN);  // newer values: cache only, not a record
  CHECK_EQ_STR(ask(f.box, f.io, "READ"), "DAT,v2,1,5eed0001,61000,43,21.5,100980");
  CHECK_EQ_STR(ask(f.box, f.io, "READ_LIVE"),
               "LIVE,v2,5eed0001,62000|bme280,publish=2,handoff_age_ms=0,acquired=unknown,"
               "humidity=44,temperature=22,pressure=!non_finite");

  advance(f.box, 121000);  // second record keeps healthy channels
  CHECK_EQ_STR(ask(f.box, f.io, "READ"), "DAT,v2,2,5eed0001,121000,44,22,!non_finite");

  bbcore::Record rec;
  advance(f.box, 181000);  // no new hand-off: repeated, not presented as new
  CHECK(f.box.registrationErrors() == 0);
  CHECK_EQ_STR(ask(f.box, f.io, "INFO"),
               "OK INFO uid=unassigned fw=0.0.0-host-test sensors=bme280 wire=v2-candidate "
               "boot=5eed0001 recording_s=60 record=3 record_ms=181000 storage=host durable=yes stored=3 "
               "capacity=256 evicted=0 pending_ram=0 lost_ram=0 write_failures=0 interrupted=0 "
               "quarantined=0 skipped=0 queue=ok hidden=0 unreadable=0 meta_rebuilt=0 "
               "loss_history=unknown net=unconfigured net_attempts=0 net_acked=0 net_failed=0 net_timeouts=0 net_retry_ms=0 net_last=none acquired_time=unknown reg_errors=0 wifi=unconfigured ip=- mac=- rssi_dbm=- wifi_ssid=42617264576972656c657373 receiver=- net_error=-");
  (void)rec;
}

static void test_never_published_and_skips() {
  Fixture f;
  advance(f.box, 61000);
  CHECK_EQ_STR(ask(f.box, f.io, "READ"), "DAT,v2,1,5eed0001,61000,!no_sample,!no_sample,!no_sample");
  advance(f.box, 61000 + 3 * 60000 + 5);  // service late by three intervals: one record
  CHECK(ask(f.box, f.io, "INFO").find(" record=2 ") != std::string::npos);
  CHECK(ask(f.box, f.io, "INFO").find(" skipped=2 ") != std::string::npos);
}

static void test_no_storage_loss_is_visible_and_latest_kept() {
  BardBoxInterface box;  // no storage adapter: RAM retry queue only
  HostStream io;
  g_host_millis = 1000;
  BardBoxSensor bme = box.addSensor("bme280");
  bme.addMeasurement("humidity", "%");
  CHECK(box.beginWithPort(io, FIRMWARE_VERSION, false));
  uint32_t t = 1000;
  for (int i = 0; i < BARDBOX_BUFFER_RAM_RECORDS + 2; ++i) {
    t += 60000;
    bme.publish(40.0f + i);
    advance(box, t);
  }
  const std::string info = ask(box, io, "INFO");
  CHECK(info.find(" storage=none durable=no stored=0 ") != std::string::npos);
  CHECK(info.find(" pending_ram=8 lost_ram=2 ") != std::string::npos);
  CHECK_EQ_STR(ask(box, io, "READ"), "DAT,v2,10,5eed0001,601000,49");
}

static void test_serial_parsing() {
  Fixture f;
  CHECK_EQ_STR(ask(f.box, f.io, "ping"), "PONG");
  CHECK_EQ_STR(ask(f.box, f.io, "  read_live  ").substr(0, 8), "LIVE,v2,");
  CHECK_EQ_STR(ask(f.box, f.io, "BOGUS"), "ERR UNKNOWN_CMD");
  std::string longLine(200, 'X');
  CHECK_EQ_STR(ask(f.box, f.io, longLine.c_str()), "");  // discarded, no reply
  CHECK_EQ_STR(ask(f.box, f.io, "PING"), "PONG");        // parser recovered
  CHECK(bbcore::parseSerialCommand("READ_LIVE") == bbcore::SerialCommand::ReadLive);
}

static void test_settings() {
  Fixture f;
  uint32_t v = 0;
  CHECK(f.box.getSetting("recording_interval_ms", v) && v == 60000);
  CHECK(!f.box.getSetting("wifi_password", v));
}


static void test_interleaved_registration() {
  BardBoxInterface box;
  HostStream io;
  g_host_millis = 1000;
  BardBoxSensor a = box.addSensor("pms_a");
  BardBoxSensor b = box.addSensor("pms_b");
  CHECK(a.addMeasurement("pm25", "ug/m3"));
  CHECK(b.addMeasurement("pm25", "ug/m3"));  // same name, other sensor
  CHECK(a.addMeasurement("pm10", "ug/m3"));
  CHECK(b.addMeasurement("pm10", "ug/m3"));
  CHECK(box.beginWithPort(io, FIRMWARE_VERSION, false));
  CHECK_EQ_STR(ask(box, io, "HEADER"),
               "HDR,v2,record,boot,captured_ms,pms_a.pm25,pms_b.pm25,pms_a.pm10,pms_b.pm10");
  CHECK(a.publish(1.0f, 2.0f).accepted);   // a: pm25=1, pm10=2
  CHECK(b.publish(10.0f, 20.0f).accepted); // b: pm25=10, pm10=20
  CHECK_EQ_STR(ask(box, io, "READ_LIVE"),
               "LIVE,v2,5eed0001,1000|pms_a,publish=1,handoff_age_ms=0,acquired=unknown,pm25=1,pm10=2"
               "|pms_b,publish=1,handoff_age_ms=0,acquired=unknown,pm25=10,pm10=20");
  advance(box, 61000);
  CHECK_EQ_STR(ask(box, io, "READ"), "DAT,v2,1,5eed0001,61000,1,10,2,20");
}

static void test_stalled_serial_does_not_stop_recording() {
  Fixture f;
  f.bme.publish(43.0f, 21.5f, 100980.0f);
  f.io.writeRoom = 0;  // host connected but not draining
  f.io.input.append("INFO\nPING\n");
  for (uint32_t t = 1000; t <= 200000; t += 1000) advance(f.box, t);
  CHECK(f.io.output.empty());                 // nothing half-written
  f.io.writeRoom = 4096;
  f.io.maxPerWrite = 7;                       // now drains, but in small pieces
  for (int i = 0; i < 400; ++i) f.box.runServiceStep();
  const std::string out = f.io.output;
  CHECK(out.find("OK INFO uid=unassigned") == 0);  // composed at t=1 s, delivered intact
  CHECK(out.find(" record=none ") != std::string::npos);
  CHECK(out.size() > 6 && out.compare(out.size() - 6, 6, "\nPONG\n") == 0);  // then the next reply
  f.io.maxPerWrite = 1u << 20;
  CHECK(ask(f.box, f.io, "INFO").find(" record=3 ") != std::string::npos);  // recorded while stalled
}

int main() {
  test_registration();
  test_publish_checks();
  test_types();
  test_records_vs_live();
  test_never_published_and_skips();
  test_no_storage_loss_is_visible_and_latest_kept();
  test_serial_parsing();
  test_settings();
  test_interleaved_registration();
  test_stalled_serial_does_not_stop_recording();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox host tests: all checks passed (simulated values, no hardware)\n");
  return 0;
}
