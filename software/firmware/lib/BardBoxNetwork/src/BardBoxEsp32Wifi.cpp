#include "BardBoxEsp32Wifi.h"
#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include "BardBoxNetworkConfig.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_tls.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <string.h>
#include <time.h>

namespace bbcore {
namespace {
class Esp32WifiIo : public WifiIo {
 public:
  bool reset() override {
    portENTER_CRITICAL(&dnsLock_);
    const bool pending = dnsPending_;
    if (!pending) { dnsComplete_ = false; dnsOk_ = false; }
    portEXIT_CRITICAL(&dnsLock_);
    // Do not reuse callback storage or create a second resolver while a
    // timed-out DNS request is still outstanding. No unbounded allocation.
    if (pending) return false;
    close();
    dnsStarted_ = false;
    return true;
  }
  bool associate(const char *ssid, const char *password) override {
    WiFi.persistent(false);  // never write credentials into Wi-Fi NVS
    WiFi.setAutoReconnect(false);  // bounded policy belongs to Network
    if (!WiFi.mode(WIFI_STA)) return false;
    WiFi.begin(ssid, password);
    return true;  // association is observed on subsequent steps
  }
  bool connected() override { return WiFi.status() == WL_CONNECTED; }
  bool clockReady(const char *server) override {
    // CA verification needs a real clock; never relax certificate checks to
    // get an uncommissioned device connected. SNTP itself runs asynchronously.
    if (time(nullptr) >= 1704067200) return true;  // 2024-01-01
    if (!ntpRequested_) { configTime(0, 0, server); ntpRequested_ = true; }
    return false;
  }
  Progress resolve(const char *host, char *out, size_t capacity) override {
    if (!dnsStarted_) {
      if (strlen(host) >= sizeof dnsHost_) return Progress::Failed;
      strcpy(dnsHost_, host);
      portENTER_CRITICAL(&dnsLock_);
      dnsPending_ = true;
      dnsComplete_ = false;
      portEXIT_CRITICAL(&dnsLock_);
      dnsStarted_ = true;
      if (tcpip_try_callback(startDns, this) != ERR_OK) finishDns(nullptr);
    }
    ip_addr_t address;
    portENTER_CRITICAL(&dnsLock_);
    const bool complete = dnsComplete_, ok = dnsOk_;
    address = dnsAddress_;
    portEXIT_CRITICAL(&dnsLock_);
    if (!complete) return Progress::Pending;
    if (!ok || !ipaddr_ntoa_r(&address, out, static_cast<int>(capacity))) return Progress::Failed;
    return Progress::Ready;
  }
  Progress connectTls(const char *ip, uint16_t port, const char *host,
                      const char *ca, bool &handshaking) override {
    if (!tls_) {
      tls_ = esp_tls_init();
      if (!tls_) return Progress::Failed;
      memset(&tlsConfig_, 0, sizeof tlsConfig_);
      tlsConfig_.non_block = true;
      // IDF 4.4 uses a NULL select timeout for zero, which waits forever.
      // One millisecond bounds each TCP readiness check; outer phase deadlines
      // remain owned by Network. See upstream esp_tls.c ESP_TLS_CONNECTING.
      tlsConfig_.timeout_ms = 1;
      tlsConfig_.cacert_buf = reinterpret_cast<const unsigned char *>(ca);
      tlsConfig_.cacert_bytes = strlen(ca) + 1;
      // Resolve separately without changing TLS identity/SNI to an IP address.
      tlsConfig_.common_name = host;
      tlsConfig_.skip_common_name = false;
    }
    // select() consumes these sets on a pending connect; re-arm them on
    // every poll (the installed IDF exposes them in esp_tls_t).
    if (tls_->conn_state == ESP_TLS_CONNECTING) {
      FD_ZERO(&tls_->rset); FD_SET(tls_->sockfd, &tls_->rset);
      FD_ZERO(&tls_->wset); FD_SET(tls_->sockfd, &tls_->wset);
    }
    const int result = esp_tls_conn_new_async(ip, strlen(ip), port, &tlsConfig_, tls_);
    handshaking = tls_->conn_state == ESP_TLS_HANDSHAKE;
    return result == 1 ? Progress::Ready : result < 0 ? Progress::Failed : Progress::Pending;
  }
  int write(const uint8_t *data, size_t count) override {
    if (!tls_) return -1;
    return translate(esp_tls_conn_write(tls_, data, count));
  }
  int read(uint8_t *data, size_t capacity) override {
    if (!tls_) return -1;
    return translate(esp_tls_conn_read(tls_, data, capacity));
  }
  void close() override {
    if (tls_) { esp_tls_conn_destroy(tls_); tls_ = nullptr; }
    // DNS cannot be cancelled safely. Its device-lifetime callback storage
    // remains owned here until it completes; reset refuses reuse meanwhile.
  }
 private:
  static int translate(ssize_t n) {
    if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) return 0;
    return n > 0 ? static_cast<int>(n) : -1;
  }
  static void startDns(void *arg) {
    Esp32WifiIo *self = static_cast<Esp32WifiIo *>(arg);
    ip_addr_t address;
    const err_t err = dns_gethostbyname(self->dnsHost_, &address, foundDns, self);
    if (err == ERR_OK) self->finishDns(&address);
    else if (err != ERR_INPROGRESS) self->finishDns(nullptr);
  }
  static void foundDns(const char *, const ip_addr_t *address, void *arg) {
    static_cast<Esp32WifiIo *>(arg)->finishDns(address);
  }
  void finishDns(const ip_addr_t *address) {
    portENTER_CRITICAL(&dnsLock_);
    dnsOk_ = address && !ip_addr_isany(address);
    if (address) dnsAddress_ = *address;
    dnsComplete_ = true;
    dnsPending_ = false;
    portEXIT_CRITICAL(&dnsLock_);
  }
  portMUX_TYPE dnsLock_ = portMUX_INITIALIZER_UNLOCKED;
  bool dnsPending_ = false, dnsComplete_ = false, dnsOk_ = false;
  bool dnsStarted_ = false, ntpRequested_ = false;
  char dnsHost_[128] = {};
  ip_addr_t dnsAddress_ = {};
  esp_tls_t *tls_ = nullptr;
  esp_tls_cfg_t tlsConfig_ = {};
};
WifiSettings compiledSettings() {
  WifiSettings s;
#ifdef BARDBOX_WIFI_SSID
  s.ssid = BARDBOX_WIFI_SSID;
#endif
#ifdef BARDBOX_WIFI_PASSWORD
  s.password = BARDBOX_WIFI_PASSWORD;
#endif
#ifdef BARDBOX_NETWORK_ENDPOINT
  s.endpoint = BARDBOX_NETWORK_ENDPOINT;
#endif
#ifdef BARDBOX_NETWORK_CA_PEM
  s.caPem = BARDBOX_NETWORK_CA_PEM;
#endif
#ifdef BARDBOX_NETWORK_BEARER_TOKEN
  s.bearer = BARDBOX_NETWORK_BEARER_TOKEN;
#endif
#ifdef BARDBOX_NETWORK_NTP_SERVER
  s.ntpServer = BARDBOX_NETWORK_NTP_SERVER;
#endif
  s.reconnectMs = BARDBOX_WIFI_RECONNECT_MS;
  return s;
}
}
WifiHttpsTransport &defaultEsp32WifiTransport() {
  static Esp32WifiIo io;
  static WifiHttpsTransport transport(io, compiledSettings());
  return transport;
}
}  // namespace bbcore
#endif
