#define BARDBOX_INTERFACE_IMPLEMENTATION
#include "BardBoxInterface.h"

#include <string.h>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define BARDBOX_INTERFACE_RTOS 1
#include "BardBoxLittleFsStorage.h"
#include "BardBoxEsp32Wifi.h"
static bbcore::LittleFsStorage littleFsStorage;
#include "BardBoxNvsSettingsStore.h"
static bbcore::NvsSettingsStore nvsSettingsStore;
#endif

bool BardBoxSensor::addMeasurement(const char *name, const char *unit, MeasurementType type) {
  return owner_ && owner_->measurements_.addMeasurement(index_, name, unit, type);
}

PublishResult BardBoxSensor::publishValues(const bbcore::ValueArg *args, size_t count) {
  if (!owner_) {
    PublishResult r;
    r.reason = "invalid_sensor";
    return r;
  }
  return owner_->measurements_.publish(index_, args, count);
}

BardBoxSensor BardBoxInterface::addSensor(const char *name) {
  const int index = begun_ ? -1 : measurements_.addSensor(name);
  return index < 0 ? BardBoxSensor() : BardBoxSensor(this, static_cast<uint8_t>(index));
}

bool BardBoxInterface::getSetting(const char *name, uint32_t &value) const {
  if (name && strcmp(name, "recording_interval_ms") == 0) {
    value = bbcore::BardBoxBuffer::recordingIntervalMs();
    return true;
  }
  return false;
}

#if defined(BARDBOX_INTERFACE_RTOS)
static void serviceTask(void *arg) {
  BardBoxInterface *self = static_cast<BardBoxInterface *>(arg);
  for (;;) {
    self->runServiceStep();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void networkTask(void *arg) {
  BardBoxInterface *self = static_cast<BardBoxInterface *>(arg);
  for (;;) {
    self->runNetworkStep();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

bool BardBoxInterface::beginWithVersion(const char *firmwareVersion) {
#if defined(BARDBOX_INTERFACE_RTOS)
  if (!storage_) storage_ = &littleFsStorage;
  if (!settingsStore_) settingsStore_ = &nvsSettingsStore;
  Serial.begin(bbcore::BardBoxSerial::configuredBaud());
  return beginWithPort(Serial, firmwareVersion, true);
#elif defined(BARDBOX_HOST_DEMO)
  // Host demo only: the stub's delay() runs the service step, and the demo
  // supplies in-memory storage.
  extern bbcore::StorageAdapter *g_host_demo_storage;
  extern bbcore::Transport *g_host_demo_transport;
  if (!storage_) storage_ = g_host_demo_storage;
  if (!transport_) transport_ = g_host_demo_transport;
  g_host_service = [](void *self) {
    static_cast<BardBoxInterface *>(self)->runServiceStep();
    static_cast<BardBoxInterface *>(self)->runNetworkStep();
  };
  g_host_service_arg = this;
  return beginWithPort(Serial, firmwareVersion, false);
#else
  (void)firmwareVersion;
  return false;  // hosts call beginWithPort with their own stream
#endif
}

bool BardBoxInterface::beginWithPort(Stream &port, const char *firmwareVersion, bool startServiceTask) {
  if (begun_ || !firmwareVersion || !*firmwareVersion) return false;
  if (!measurements_.freeze()) return false;
  firmwareVersion_ = firmwareVersion;
#if defined(BARDBOX_INTERFACE_RTOS)
  bootId_ = esp_random();
#else
  bootId_ = hostBootId_;  // fixed on hosts so tests are deterministic
#endif
  // Compiled values are defaults; saved settings override them from this boot.
#if defined(BARDBOX_WIFI_SSID)
  const char *defaultSsid = BARDBOX_WIFI_SSID;
#else
  const char *defaultSsid = "";
#endif
#if defined(BARDBOX_WIFI_PASSWORD)
  const char *defaultPassword = BARDBOX_WIFI_PASSWORD;
#else
  const char *defaultPassword = "";
#endif
  settings_.begin(settingsStore_, BARDBOX_DEVICE_UID, defaultSsid, defaultPassword);
  // New records carry this boot's UID; saved records keep their own.
  if (!buffer_.setAttribution(settings_.active().uid, firmwareVersion, bbcore::kWireProtocol)) return false;
#if defined(BARDBOX_INTERFACE_RTOS)
  if (!buffer_.begin(millis(), bootId_, storage_, esp_random())) return false;
#else
  if (!buffer_.begin(millis(), bootId_, storage_, bootId_)) return false;
#endif
  mailbox_.begin();
  bbcore::NetworkSettings networkSettings;
  networkSettings.ssid = settings_.active().ssid;  // diagnostics only
#if defined(BARDBOX_INTERFACE_RTOS)
  if (!transport_ && networkSettings.enabled) {
    const bbcore::SettingsValues &active = settings_.active();
    bbcore::WifiHttpsTransport &wifi = bbcore::defaultEsp32WifiTransport(active.ssid, active.password);
    networkSettings.endpointConfigured = wifi.configured();
    if (wifi.configured()) transport_ = &wifi;
  }
#endif
  network_.begin(networkSettings, transport_, millis(), mailbox_);
  serial_.begin(port, *this);
  serial_.useSettings(settings_);
  begun_ = true;
#if defined(BARDBOX_INTERFACE_RTOS)
  if (startServiceTask && xTaskCreate(serviceTask, "bardbox", 8192, this, 1, nullptr) != pdPASS) {
    begun_ = false;  // no running service, so no Serial is promised
    return false;
  }
  // Separate task isolates transport progress from the service task.
  // Actual scheduling and storage latency still require device measurements.
  if (startServiceTask && network_.active() &&
      xTaskCreate(networkTask, "bardbox-net", 6144, this, 1, nullptr) != pdPASS)
    network_.begin(bbcore::NetworkSettings(), nullptr, millis(), mailbox_);  // reports no_transport
#else
  (void)startServiceTask;
#endif
  return measurements_.registrationErrors() == 0;
}

void BardBoxInterface::runServiceStep() {
  if (!begun_) return;
  serial_.service(1);
  buffer_.service(millis(), measurements_);
  deliveryOwner_.step(buffer_, mailbox_);
}

void BardBoxInterface::runNetworkStep() {
  if (begun_) network_.step(millis(), mailbox_);
}

void BardBoxInterface::status(bbcore::SerialStatus &out) {
  out.uid = settings_.active().uid;
  out.firmware = firmwareVersion_;
  out.bootId = bootId_;
  out.recordingIntervalS = BARDBOX_RECORDING_INTERVAL_S;
  const bbcore::BufferStatus b = buffer_.status();
  out.storage = b.storage;
  out.durable = b.durable;
  out.stored = b.stored;
  out.capacity = b.capacity;
  out.evicted = b.evicted;
  out.pendingRam = b.pendingRam;
  out.lostRam = b.lostRam;
  out.writeFailures = b.writeFailures;
  out.interrupted = b.interrupted;
  out.quarantined = b.quarantined;
  out.skippedBoundaries = b.skippedBoundaries;
  out.queueState = bbcore::queueStateName(b.state);
  out.hidden = b.hidden;
  out.unreadable = b.unreadable;
  out.metaRebuilt = b.metaRebuilt;
  out.lossHistoryKnown = b.lossHistoryKnown;
  const bbcore::NetworkStats n = mailbox_.stats();
  out.network = bbcore::networkStateName(n.state);
  out.netAttempts = n.attempts;
  out.netAcked = n.acked;
  out.netFailed = n.failed;
  out.netTimeouts = n.timeouts;
  out.netRetryMs = n.retryInMs;
  memcpy(out.netLast, n.lastOutcome, sizeof out.netLast);
  out.link = n.link;
  out.registrationErrors = measurements_.registrationErrors();
}
