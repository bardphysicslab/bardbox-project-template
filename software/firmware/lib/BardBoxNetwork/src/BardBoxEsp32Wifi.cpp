#include "BardBoxEsp32Wifi.h"
#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include "BardBoxNetworkConfig.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_tls.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <string.h>
#include <time.h>

namespace bbcore {
namespace {
// Classifies a failed esp_tls_conn_new_async from the error esp-tls recorded,
// not from conn_state: in IDF 4.4.7 one call can go CONNECTING -> HANDSHAKE ->
// FAIL. esp_tls_code is set only by mbedtls and flags only by certificate
// verification. In non-blocking mode IDF 4.4.7 does not check SO_ERROR, so a
// refused TCP connection appears as an mbedtls network error (-0x004x/-0x005x)
// during the handshake ("tls:" with that code). Numbers only.
void classifyConnectFailure(esp_err_t last, int code, int flags, char *out, size_t cap) {
  if (last == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED || code != 0 || flags != 0)
    snprintf(out, cap, "tls:-0x%04x:0x%x", static_cast<unsigned>(code < 0 ? -code : code), static_cast<unsigned>(flags));
  else if (last >= ESP_ERR_MBEDTLS_CERT_PARTLY_OK && last <= ESP_ERR_MBEDTLS_SSL_TICKET_SETUP_FAILED)
    snprintf(out, cap, "tls_setup:0x%x", static_cast<unsigned>(last));  // for example an unparsable CA PEM
  else if (last >= ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME && last <= ESP_ERR_ESP_TLS_TCP_CLOSED_FIN)
    snprintf(out, cap, "tcp:0x%x", static_cast<unsigned>(last));
  else
    snprintf(out, cap, "connect:0x%x", static_cast<unsigned>(last));  // ambiguous
}
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
      failure_[0] = 0;
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
    if (result < 0) {  // keep the reason; the record goes with the connection
      int code = 0, flags = 0;
      const esp_err_t last = esp_tls_get_and_clear_last_error(tls_->error_handle, &code, &flags);
      classifyConnectFailure(last, code, flags, failure_, sizeof failure_);
    }
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
  void connectFailure(char *detail, size_t cap) override { snprintf(detail, cap, "%s", failure_); }
  bool linkInfo(char *ip, size_t ipCap, char *mac, size_t macCap, int &rssiDbm) override {
    uint8_t m[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, m) == ESP_OK)  // fails until Wi-Fi has started
      snprintf(mac, macCap, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    else
      mac[0] = 0;
    ip[0] = 0;
    if (WiFi.status() != WL_CONNECTED) return false;
    const IPAddress a = WiFi.localIP();
    snprintf(ip, ipCap, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    rssiDbm = WiFi.RSSI();
    return true;
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
  char failure_[32] = {};
  esp_tls_cfg_t tlsConfig_ = {};
};
WifiSettings compiledSettings(const char *ssid, const char *password) {
  WifiSettings s;
  s.ssid = ssid;
  s.password = password && *password ? password : nullptr;  // empty: open network
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
WifiHttpsTransport &defaultEsp32WifiTransport(const char *ssid, const char *password) {
  static Esp32WifiIo io;
  static WifiHttpsTransport transport(io, compiledSettings(ssid, password));
  return transport;
}
}  // namespace bbcore
#endif
