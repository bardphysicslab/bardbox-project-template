#include <cassert>
#include "BardBoxWiFiRecovery.h"

FakeSerial Serial;
FakeESP ESP;
FakeWiFi WiFi;

int main() {
  bardbox::WiFiRecovery recovery;
  int disconnects = 0;
  int flushes = 0;
  auto tick = [&](uint32_t now) {
    recovery.service("test", "test", now, [&] { ++disconnects; },
                     [&] { ++flushes; return true; });
  };

  tick(1000);
  assert(disconnects == 1 && WiFi.attempts == 1);
  tick(30999);
  assert(WiFi.attempts == 1);
  tick(31000);
  assert(WiFi.attempts == 2 && disconnects == 1);

  WiFi.state = WL_CONNECTED;
  tick(31001);
  assert(!recovery.offline() && recovery.offlineMs(31001) == 0);
  WiFi.state = WL_DISCONNECTED;
  tick(50000);
  assert(disconnects == 2 && WiFi.attempts == 3);
  tick(949999);
  assert(ESP.restarts == 0);
  tick(950000);
  tick(950001);
  assert(ESP.restarts == 1 && flushes == 1);

  // A queued-record project may disable restart until persistence is proven.
  bardbox::WiFiRecovery noRestart({30000, 0});
  noRestart.service("test", "test", 0, [] {}, [] { return true; });
  noRestart.service("test", "test", 3600000, [] {}, [] { return true; });
  assert(ESP.restarts == 1);

  // Failed persistence / exhausted restart budget vetoes reboot, without
  // suppressing association retries or calling the gate on every loop.
  bardbox::WiFiRecovery veto({30000, 60000});
  int vetoes = 0;
  auto vetoTick = [&](uint32_t now) {
    veto.service("test", "test", now, [] {}, [&] { ++vetoes; return false; });
  };
  const int attemptsBeforeVeto = WiFi.attempts;
  vetoTick(0);
  vetoTick(60000);
  vetoTick(60001);
  assert(vetoes == 1 && ESP.restarts == 1);
  vetoTick(90000);
  assert(vetoes == 2 && WiFi.attempts == attemptsBeforeVeto + 3);

  // Unsigned time subtraction keeps retry timing correct across millis wrap.
  WiFi.state = WL_DISCONNECTED;
  bardbox::WiFiRecovery wrap({30000, 0});
  const int beforeWrap = WiFi.attempts;
  wrap.service("test", "test", UINT32_MAX - 1000, [] {}, [] { return true; });
  wrap.service("test", "test", 10000, [] {}, [] { return true; });
  assert(WiFi.attempts == beforeWrap + 1);
  wrap.service("test", "test", 28999, [] {}, [] { return true; });
  assert(WiFi.attempts == beforeWrap + 2);
}
