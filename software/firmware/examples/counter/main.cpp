// Synthetic node for the isolated Wi-Fi demo: an increasing counter, not a
// sensor reading. No timestamp code here: BardBox stamps each record with the
// UTC time it was created once the clock is synchronized over NTP.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION ""  // Student: set your firmware version before a device build.
#endif
#include <Arduino.h>
#include <BardBoxInterface.h>

BardBoxInterface bardbox;
BardBoxSensor demo;
long counter = 0;

void setup() {
  demo = bardbox.addSensor("demo");
  demo.addMeasurement("counter", "1", MeasurementType::Integer);
  bardbox.begin();
}

void loop() {
  demo.publish(++counter);  // each record keeps the latest count at its boundary
  delay(500);               // recording, delivery and NTP progress internally
}
