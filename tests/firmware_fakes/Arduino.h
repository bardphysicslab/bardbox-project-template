#pragma once
#include <cstdint>

struct FakeSerial {
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
  void println() {}
  void flush() {}
};
extern FakeSerial Serial;

struct FakeESP {
  int restarts = 0;
  void restart() { ++restarts; }
};
extern FakeESP ESP;
