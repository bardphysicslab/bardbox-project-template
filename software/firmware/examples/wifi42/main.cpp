// Synthetic node for an isolated Wi-Fi test; not a real sensor reading.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION ""  // Student: set your firmware version before a device build.
#endif

#include <Arduino.h>
#include <BardBoxInterface.h>

BardBoxInterface bardbox;
BardBoxSensor demo;
void setup() {
  demo = bardbox.addSensor("demo");
  demo.addMeasurement("answer", "1");
  bardbox.begin();
}
void loop() {
  demo.publish(42.0f);
  delay(500);  // recording and Wi-Fi progress internally
}
