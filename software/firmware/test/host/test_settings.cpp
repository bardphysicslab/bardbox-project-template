// Host tests for saved device settings (UID, Wi-Fi SSID/password) and the
// CONFIG_* serial commands. Simulated store, storage and time only: not NVS,
// flash, Wi-Fi or hardware evidence.
#define FIRMWARE_VERSION "0.0.0-settings-test"
#include <BardBoxInterface.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "FakeSettingsStore.h"
#include "FakeStorage.h"

uint32_t g_host_millis = 0;

static int failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++failures;                                                                \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                            \
  } while (0)
#define CHECK_EQ(a, b)                                                                                       \
  do {                                                                                                       \
    const std::string _a = (a), _b = (b);                                                                    \
    if (_a != _b) {                                                                                          \
      ++failures;                                                                                            \
      fprintf(stderr, "%s:%d: expected\n  %s\ngot\n  %s\n", __FILE__, __LINE__, _b.c_str(), _a.c_str()); \
    }                                                                                                        \
  } while (0)

static const char *kPassword = "correct horse battery";  // synthetic

static std::string hex(const std::string &s) {
  if (s.empty()) return "-";
  std::string out;
  char b[3];
  for (unsigned char c : s) {
    snprintf(b, sizeof b, "%02x", c);
    out += b;
  }
  return out;
}

// Everything the device ever wrote, for the never-echo check.
static std::string g_transcript;

static std::string ask(BardBoxInterface &box, HostStream &io, const std::string &cmd) {
  io.output.clear();
  io.input.append(cmd).append("\n");
  box.runServiceStep();
  g_transcript += io.output;
  std::string out = io.output;
  if (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

static std::string field(const std::string &line, const char *key) {
  const std::string k = std::string(key) + "=";
  size_t at = line.find("," + k);
  if (at == std::string::npos) at = line.find(" " + k);
  if (at == std::string::npos) return "<missing>";
  at += k.size() + 1;
  const size_t end = line.find_first_of(", ", at);
  return line.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

struct Device {
  BardBoxInterface box;
  HostStream io;
  Device(FakeSettingsStore *settings, FakeStorage *storage, uint32_t bootId = 0x5eed0001u,
         const char *fw = FIRMWARE_VERSION) {
    BardBoxSensor bme = box.addSensor("bme280");
    bme.addMeasurement("humidity", "%");
    box.useSettingsStore(settings);
    box.useStorage(storage);
    box.setHostBootId(bootId);
    CHECK(box.beginWithPort(io, fw, false));
  }
  std::string ask(const std::string &cmd) { return ::ask(box, io, cmd); }
  uint32_t revision() { return static_cast<uint32_t>(atol(field(ask("CONFIG_GET"), "revision").c_str())); }
  // One complete update; returns the COMMIT reply (or the first refusal).
  std::string update(const std::string &uid, const std::string &ssid, const std::string &password) {
    std::string r = ask("CONFIG_BEGIN " + std::to_string(revision()));
    if (r.compare(0, 15, "CONFIG_BEGIN_OK") != 0) return r;
    const std::string sets[3][2] = {{"uid", uid}, {"wifi_ssid", ssid}, {"wifi_password", password}};
    for (const auto &s : sets) {
      r = ask("CONFIG_SET " + s[0] + " " + hex(s[1]));
      if (r.compare(0, 13, "CONFIG_SET_OK") != 0) return r;
    }
    return ask("CONFIG_COMMIT");
  }
};

static void test_schema_and_defaults() {
  FakeSettingsStore store;
  Device d(&store, nullptr);
  CHECK_EQ(d.ask("CONFIG_SCHEMA"),
           "CONFIG_SCHEMA,version=1,encoding=hex,activation=next_boot,"
           "fields=uid:1:32:public;wifi_ssid:0:32:public;wifi_password:8:63:secret:empty_open");
  CHECK_EQ(d.ask("CONFIG_GET"),
           "CONFIG,version=1,store=none,revision=0,uid=unassigned,wifi_ssid=" + hex("BardWireless") +
           ",wifi_password=empty,active_uid=unassigned,active_wifi_ssid=" + hex("BardWireless") +
           ",active_wifi_password=empty,pending=0,activation=next_boot");  // no config.h: open BardWireless
  CHECK_EQ(d.ask("config_get"), d.ask("CONFIG_GET"));  // command word is case-insensitive
}

// Saved values activate at the next start and survive restarts and an
// ordinary reflash (different firmware, same store).
static void test_save_restart_and_reflash() {
  FakeSettingsStore store;
  {
    Device d(&store, nullptr);
    CHECK_EQ(d.update("bb-tst-air-001", "BardTestNet", kPassword),
             "CONFIG_COMMIT_OK,revision=1,activation=next_boot");
    const std::string get = d.ask("CONFIG_GET");
    CHECK_EQ(field(get, "uid"), "bb-tst-air-001");
    CHECK_EQ(field(get, "wifi_ssid"), hex("BardTestNet"));
    CHECK_EQ(field(get, "wifi_password"), "set");
    CHECK_EQ(field(get, "active_uid"), "unassigned");  // this boot is unchanged
    CHECK_EQ(field(get, "pending"), "1");
    CHECK_EQ(field(d.ask("INFO"), "uid"), "unassigned");
  }
  {
    Device d(&store, nullptr, 0x5eed0002u);
    const std::string get = d.ask("CONFIG_GET");
    CHECK_EQ(field(get, "store"), "saved");
    CHECK_EQ(field(get, "active_uid"), "bb-tst-air-001");
    CHECK_EQ(field(get, "active_wifi_ssid"), hex("BardTestNet"));
    CHECK_EQ(field(get, "active_wifi_password"), "set");
    CHECK_EQ(field(get, "pending"), "0");
    CHECK_EQ(field(d.ask("INFO"), "uid"), "bb-tst-air-001");
    // Repeatable: change the UID again and switch to an open network.
    CHECK_EQ(d.update("bb-tst-air-002", "OpenLab", ""), "CONFIG_COMMIT_OK,revision=2,activation=next_boot");
  }
  {
    Device d(&store, nullptr, 0x5eed0003u, "9.9.9-reflashed");
    const std::string get = d.ask("CONFIG_GET");
    CHECK_EQ(field(get, "active_uid"), "bb-tst-air-002");
    CHECK_EQ(field(get, "active_wifi_password"), "empty");
    CHECK_EQ(field(get, "revision"), "2");
  }
  printf("save/restart/reflash: revision 1 then 2 activated at next start; saves=%d\n", store.saves);
}

// Every refused value names its field, ends the update and saves nothing.
static void test_rejected_settings() {
  FakeSettingsStore store;
  Device d(&store, nullptr);
  CHECK_EQ(d.update("bb-tst-air-001", "Lab", kPassword), "CONFIG_COMMIT_OK,revision=1,activation=next_boot");
  const std::string before = store.bytes;
  const struct {
    const char *field;
    std::string value;
  } bad[] = {
      {"uid", "has space"}, {"uid", std::string(33, 'a')}, {"uid", "bb/slash"}, {"uid", ""},
      {"wifi_ssid", std::string(33, 's')}, {"wifi_ssid", "tab\there"},
      {"wifi_password", "short7!"}, {"wifi_password", std::string(64, 'p')}, {"wifi_password", "new\nline1"},
  };
  for (const auto &b : bad) {
    CHECK_EQ(d.ask("CONFIG_BEGIN 1"), "CONFIG_BEGIN_OK,revision=1");
    CHECK_EQ(d.ask(std::string("CONFIG_SET ") + b.field + " " + hex(b.value)),
             std::string("ERR,CONFIG_VALUE,field=") + b.field);
    CHECK_EQ(d.ask("CONFIG_COMMIT"), "ERR,CONFIG_NO_TRANSACTION");  // the refusal ended it
  }
  // Cross-field: a password without a network.
  CHECK_EQ(d.ask("CONFIG_BEGIN 1"), "CONFIG_BEGIN_OK,revision=1");
  CHECK_EQ(d.ask("CONFIG_SET wifi_ssid -"), "CONFIG_SET_OK,field=wifi_ssid");
  CHECK_EQ(d.ask("CONFIG_COMMIT"), "ERR,CONFIG_VALUE,field=wifi_password");
  // Malformed and unknown input.
  CHECK_EQ(d.ask("CONFIG_BEGIN 1"), "CONFIG_BEGIN_OK,revision=1");
  CHECK_EQ(d.ask("CONFIG_SET name 41"), "ERR,CONFIG_FIELD");
  CHECK_EQ(d.ask("CONFIG_BEGIN 1"), "CONFIG_BEGIN_OK,revision=1");
  CHECK_EQ(d.ask("CONFIG_SET uid 4g"), "ERR,CONFIG_VALUE,field=uid");
  CHECK_EQ(d.ask("CONFIG_SET uid"), "ERR,CONFIG_COMMAND");
  CHECK_EQ(d.ask("CONFIG_BEGIN x"), "ERR,CONFIG_COMMAND");
  CHECK_EQ(d.ask("CONFIG_BEGIN 0"), "ERR,CONFIG_REVISION,revision=1");  // stale read
  CHECK_EQ(d.ask("CONFIG_NOPE"), "ERR,CONFIG_COMMAND");
  CHECK(store.bytes == before);
  CHECK_EQ(field(d.ask("CONFIG_GET"), "revision"), "1");
}

// A failed save keeps the previous valid settings, now and after restart.
static void test_save_failure_keeps_previous() {
  FakeSettingsStore store;
  {
    Device d(&store, nullptr);
    CHECK_EQ(d.update("bb-tst-air-001", "Lab", kPassword), "CONFIG_COMMIT_OK,revision=1,activation=next_boot");
    store.failSaves = 1;
    CHECK_EQ(d.update("bb-tst-air-009", "Other", ""), "ERR,CONFIG_STORE,revision=1");
    const std::string get = d.ask("CONFIG_GET");
    CHECK_EQ(field(get, "uid"), "bb-tst-air-001");
    CHECK_EQ(field(get, "wifi_password"), "set");
    // A save that reaches the store but reports failure is reported as saved:
    // the reply follows what the store holds, read back.
    store.saveLiesFailed = true;
    CHECK_EQ(d.update("bb-tst-air-002", "Lab", kPassword), "CONFIG_COMMIT_OK,revision=2,activation=next_boot");
    store.saveLiesFailed = false;
  }
  Device d(&store, nullptr, 0x5eed0002u);
  CHECK_EQ(field(d.ask("CONFIG_GET"), "active_uid"), "bb-tst-air-002");
  // Unreadable or corrupt saved settings: ignored in favour of the defaults.
  store.bytes[10] ^= 0x01;
  Device corrupt(&store, nullptr, 0x5eed0003u);
  const std::string get = corrupt.ask("CONFIG_GET");
  CHECK_EQ(field(get, "store"), "invalid");
  CHECK_EQ(field(get, "active_uid"), "unassigned");
  store.failLoads = true;
  Device unreadable(&store, nullptr, 0x5eed0004u);
  CHECK_EQ(field(unreadable.ask("CONFIG_GET"), "store"), "invalid");
}

static void test_transaction_rules() {
  FakeSettingsStore store;
  Device d(&store, nullptr);
  CHECK_EQ(d.ask("CONFIG_SET uid 41"), "ERR,CONFIG_NO_TRANSACTION");
  CHECK_EQ(d.ask("CONFIG_COMMIT"), "ERR,CONFIG_NO_TRANSACTION");
  CHECK_EQ(d.ask("CONFIG_BEGIN 0"), "CONFIG_BEGIN_OK,revision=0");
  CHECK_EQ(d.ask("CONFIG_ABORT"), "CONFIG_ABORT_OK");
  CHECK_EQ(d.ask("CONFIG_COMMIT"), "ERR,CONFIG_NO_TRANSACTION");
  CHECK_EQ(d.ask("CONFIG_BEGIN 0"), "CONFIG_BEGIN_OK,revision=0");
  g_host_millis += bbcore::BardBoxSettings::kTransactionTimeoutMs + 1;
  CHECK_EQ(d.ask("CONFIG_SET uid " + hex("bb-late")), "ERR,CONFIG_EXPIRED");
  CHECK(store.saves == 0);
  // Fields not sent keep their saved (or default) values.
  CHECK_EQ(d.ask("CONFIG_BEGIN 0"), "CONFIG_BEGIN_OK,revision=0");
  CHECK_EQ(d.ask("CONFIG_SET uid " + hex("bb-only-uid")), "CONFIG_SET_OK,field=uid");
  CHECK_EQ(d.ask("CONFIG_COMMIT"), "CONFIG_COMMIT_OK,revision=1,activation=next_boot");
  CHECK_EQ(field(d.ask("CONFIG_GET"), "wifi_ssid"), hex("BardWireless"));  // the default, kept
  // Without a store nothing can be saved, and the tool is told so.
  Device none(nullptr, nullptr);
  CHECK_EQ(field(none.ask("CONFIG_GET"), "store"), "unavailable");
  CHECK_EQ(none.ask("CONFIG_BEGIN 0"), "ERR,CONFIG_UNAVAILABLE");
}

// Lines are bounded: the longest legal command fits; longer ones are
// discarded whole without a reply. Firmware without the capability answers
// as before.
static void test_serial_bounds_and_legacy() {
  FakeSettingsStore store;
  Device d(&store, nullptr);
  CHECK_EQ(d.ask("CONFIG_BEGIN 0"), "CONFIG_BEGIN_OK,revision=0");
  const std::string longest = "CONFIG_SET wifi_password " + hex(std::string(63, 'x'));
  CHECK(longest.size() <= bbcore::BardBoxSerial::kMaxLine);
  CHECK_EQ(d.ask(longest), "CONFIG_SET_OK,field=wifi_password");
  CHECK_EQ(d.ask("CONFIG_SET wifi_password " + hex(std::string(63, 'x')) + std::string(20, '0')), "");
  CHECK_EQ(d.ask("PING"), "PONG");  // the discarded line did not disturb the next one
  CHECK_EQ(d.ask("CONFIG_ABORT"), "CONFIG_ABORT_OK");

  struct NoSource : bbcore::SerialDataSource {
    void status(bbcore::SerialStatus &) override {}
    uint8_t sensorCount() override { return 0; }
    const char *sensorName(uint8_t) override { return ""; }
    uint8_t channelCount() override { return 0; }
    const char *channelName(uint8_t) override { return ""; }
    uint8_t channelSensor(uint8_t) override { return 0; }
    bool latestRecord(bbcore::Record &) override { return false; }
    uint8_t liveChannels(bbcore::RecordChannel *, uint32_t &) override { return 0; }
  } source;
  static bbcore::BardBoxSerial legacy;
  HostStream io;
  legacy.begin(io, source);
  io.input = "CONFIG_GET\n";
  legacy.service(1);
  CHECK_EQ(io.output, "ERR UNKNOWN_CMD\n");
}

// Changing the UID never relabels saved records: queued records keep the UID
// they were recorded with; records made after the restart carry the new one.
static void test_records_keep_original_uid() {
  FakeSettingsStore store;
  FakeStorage storage;
  g_host_millis = 1000;
  {
    Device d(&store, &storage);
    for (int i = 0; i < 130; ++i) {  // past two recording boundaries (60 s)
      g_host_millis += 1000;
      d.box.runServiceStep();
    }
    CHECK(d.box.bufferForDelivery().status().stored == 2);
    CHECK_EQ(d.update("bb-tst-air-777", "", ""), "CONFIG_COMMIT_OK,revision=1,activation=next_boot");
    for (int i = 0; i < 60; ++i) {  // a record after saving, before restart
      g_host_millis += 1000;
      d.box.runServiceStep();
    }
  }
  Device d(&store, &storage, 0x5eed0002u);
  for (int i = 0; i < 61; ++i) {
    g_host_millis += 1000;
    d.box.runServiceStep();
  }
  bbcore::BardBoxBuffer &buffer = d.box.bufferForDelivery();
  static bbcore::Record r;
  bbcore::QueueToken token;
  std::string uids;
  while (buffer.peekOldest(r, token)) {
    uids += std::string(r.attribution.uid) + " ";
    CHECK(buffer.acknowledge(token) == bbcore::BardBoxBuffer::AckResult::Removed);
  }
  CHECK_EQ(uids, "unassigned unassigned unassigned bb-tst-air-777 ");
  printf("record attribution across UID change: %s\n", uids.c_str());
}

int main() {
  test_schema_and_defaults();
  test_save_restart_and_reflash();
  test_rejected_settings();
  test_save_failure_keeps_previous();
  test_transaction_rules();
  test_serial_bounds_and_legacy();
  test_records_keep_original_uid();
  // Nothing the device wrote contains a password, plain or hex-encoded.
  CHECK(g_transcript.find(kPassword) == std::string::npos);
  CHECK(g_transcript.find(hex(kPassword)) == std::string::npos);
  CHECK(g_transcript.find(hex(std::string(63, 'x'))) == std::string::npos);
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("bardbox settings tests: all checks passed (simulated store, storage and time)\n");
  return 0;
}
