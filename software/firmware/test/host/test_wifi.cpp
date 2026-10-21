// Controlled Wi-Fi/TLS I/O only: no sockets, radios, credentials or services.
#include "BardBoxWifiTransport.h"
#include <algorithm>
#include <stdio.h>
#include <string>
#include <string.h>
using namespace bbcore;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#c); } } while(0)
class FakeWifi : public WifiIo {
 public:
  bool linked = false, clock = true, resetOk = true, tlsFails = false;
  bool dnsFails = false, writeFails = false, eof = false;
  int starts = 0, closes = 0, connects = 0;
  std::string written, host, ca;
  std::string response = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
  size_t readAt = 0, writeChunk = 7, readChunk = 1;
  bool reset() override { return resetOk; }
  bool associate(const char *, const char *) override { ++starts; linked = true; return true; }
  bool connected() override { return linked; }
  bool clockReady(const char *) override { return clock; }
  Progress resolve(const char *name, char *out, size_t cap) override {
    host = name;
    if (dnsFails) return Progress::Failed;
    snprintf(out,cap,"203.0.113.1"); return Progress::Ready;
  }
  Progress connectTls(const char *ip, uint16_t port, const char *name,
                      const char *pem, bool &handshake) override {
    CHECK(strcmp(ip,"203.0.113.1") == 0 && port == 8443);
    CHECK(host == name);
    ca = pem;
    handshake = ++connects == 1;
    return tlsFails ? Progress::Failed : handshake ? Progress::Pending : Progress::Ready;
  }
  int write(const uint8_t *buf, size_t len) override {
    if (writeFails) return -1;
    const size_t n=std::min(writeChunk,len);
    written.append(reinterpret_cast<const char *>(buf),n); return static_cast<int>(n);
  }
  int read(uint8_t *buf, size_t cap) override {
    if (readAt == response.size()) return eof ? -1 : 0;
    const size_t n=std::min(std::min(cap,readChunk),response.size()-readAt);
    memcpy(buf,response.data()+readAt,n); readAt+=n; return static_cast<int>(n);
  }
  void close() override { ++closes; }
  bool failTls = true; int failCode = 0x2700, failFlags = 0x4;
  void connectFailure(bool &tls, int &code, int &flags) override { tls=failTls; code=failCode; flags=failFlags; }
  bool linkInfo(char *ip, size_t ipCap, char *mac, size_t macCap, int &rssi) override {
    snprintf(mac,macCap,"02:00:5e:10:20:30");
    if (!linked) { ip[0]=0; return false; }
    snprintf(ip,ipCap,"192.0.2.10"); rssi=-58; return true;
  }
};
static WifiSettings settings() {
  WifiSettings s;
  s.ssid="synthetic-network";
  s.endpoint="https://receiver.invalid:8443/test";
  s.caPem="synthetic trust anchor, not a real certificate";
  s.bearer="synthetic-test-token";
  return s;
}
static TransportOutcome run(WifiHttpsTransport &t) {
  TransportOutcome out;
  for(uint32_t now=0;now<30000;now+=10)
    if(t.step(now,out)==TransportPhase::Done) return out;
  CHECK(false); return out;
}
static void test_configuration() {
  const char *urls[]={"http://receiver.invalid/","https://user@receiver.invalid/",
    "https://receiver.invalid:65536/","https://receiver.invalid:0/",
    "https://receiver.invalid/#fragment","https://receiver.invalid/ x",
    "https://receiver.invalid/\r\nAuthorization: forged","https://[::1]/"};
  for(auto url: urls) { FakeWifi io; auto s=settings(); s.endpoint=url;
    WifiHttpsTransport t(io,s); CHECK(!t.configured());
    CHECK(!t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0)); CHECK(io.starts==0); }
  FakeWifi io;
  auto s=settings(); s.caPem=nullptr; WifiHttpsTransport noCa(io,s); CHECK(!noCa.configured());
  s=settings(); s.ssid=""; WifiHttpsTransport noSsid(io,s); CHECK(!noSsid.configured());
  s=settings(); s.bearer="secret\r\nInjected: x"; WifiHttpsTransport injection(io,s); CHECK(!injection.configured());
  s=settings(); s.endpoint="https://receiver.invalid/";
  WifiHttpsTransport normal(io,s); CHECK(normal.configured());
}
static void test_partial_io() {
  FakeWifi io; auto s=settings(); WifiHttpsTransport t(io,s);
  CHECK(t.configured());
  uint8_t body[]="{\"value\":42}";
  CHECK(t.start(17,body,sizeof body-1,0));
  body[1]='X';  // caller's body can be reused immediately
  const auto out=run(t);
  CHECK(out.kind==TransportOutcome::Kind::HttpStatus && out.httpStatus==201);
  CHECK(out.attemptId==17 && out.endpointVerified);
  CHECK(io.written.find("POST /test HTTP/1.1\r\nHost: receiver.invalid:8443\r\n") == 0);
  CHECK(io.written.find("Authorization: Bearer synthetic-test-token\r\n")!=std::string::npos);
  CHECK(io.written.find("Content-Length: 12\r\n")!=std::string::npos);
  CHECK(io.written.substr(io.written.size()-12)=="{\"value\":42}");
  CHECK(io.ca==s.caPem && io.connects==2 && io.starts==1);
}
static void test_bad_responses() {
  const char *responses[]={"HTTP/1.1 200 OK\n\n", "HTTP/1.1 20 OK\r\n\r\n",
    "HTTP/1.1 200 OK\r\nBad Header\r\n\r\n", "HTTP/1.1 200 OK\rX",
    "HTTP/1.1 200 OK\r\nContent-Length: nope\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\ncontent-length: 2\r\n\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
    "HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n"};
  for(auto response:responses) { FakeWifi io;io.response=response;
    WifiHttpsTransport t(io,settings()); CHECK(t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0));
    auto out=run(t);CHECK(out.kind==TransportOutcome::Kind::Malformed && !out.endpointVerified); }
  FakeWifi io;io.response="HTTP/1.1 200 OK\r\nX: "+std::string(600,'x')+"\r\n\r\n";
  WifiHttpsTransport t(io,settings()); CHECK(t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0));
  CHECK(run(t).kind==TransportOutcome::Kind::Malformed);
  FakeWifi partial;partial.response="HTTP/1.1 200 OK\r\n"; partial.eof=true;
  WifiHttpsTransport incomplete(partial,settings());CHECK(incomplete.start(2,reinterpret_cast<const uint8_t *>("42"),2,0));
  CHECK(run(incomplete).kind==TransportOutcome::Kind::NoResponse);
}
static void test_failures_and_cancel() {
  for(int i=0;i<3;++i) { FakeWifi io;io.dnsFails=i==0;io.tlsFails=i==1;io.writeFails=i==2;
    WifiHttpsTransport t(io,settings()); CHECK(t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0));
    auto out=run(t);CHECK(out.kind==TransportOutcome::Kind::ConnectionFailed && !out.endpointVerified); }
  FakeWifi io;io.clock=false;
  WifiHttpsTransport t(io,settings());CHECK(t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0));
  TransportOutcome out;
  t.step(0,out);CHECK(t.step(10,out)==TransportPhase::Associating);CHECK(io.connects==0);
  t.abort();CHECK(t.step(20,out)==TransportPhase::Idle);
  io.resetOk=false;CHECK(!t.start(2,reinterpret_cast<const uint8_t *>("42"),2,30));
  io.resetOk=true;io.linked=false;
  CHECK(t.start(3,reinterpret_cast<const uint8_t *>("42"),2,100));
  t.step(100,out);CHECK(io.starts==1);  // reconnect cooldown survives attempts
  t.step(30000,out);CHECK(io.starts==2);
}
// Diagnostics never carry the endpoint path, query, bearer token or password.
static void test_link_status() {
  FakeWifi io; auto s=settings();
  s.endpoint="https://receiver.invalid:8443/ingest/private-path?token=query-secret";
  s.password="wifi-password-secret";
  WifiHttpsTransport t(io,s); CHECK(t.configured());
  ConnectivityStatus c; io.linked=true; t.linkStatus(c);
  CHECK(c.wifi==WifiState::Connected && strcmp(c.ip,"192.0.2.10")==0 && c.rssiKnown && c.rssiDbm==-58);
  CHECK(strcmp(c.receiver,"https://receiver.invalid:8443")==0);
  const std::string all=std::string(c.receiver)+c.ip+c.mac+c.ssid;
  for (const char *secret: {"private-path","query-secret","synthetic-test-token","wifi-password-secret"})
    CHECK(all.find(secret)==std::string::npos);
  ConnectivityStatus d; io.linked=false; t.linkStatus(d);
  CHECK(d.wifi==WifiState::Disconnected && !d.rssiKnown && d.ip[0]==0 && strcmp(d.mac,"02:00:5e:10:20:30")==0);
}
// Connection failures name their stage with numbers only (template#36).
static void test_failure_detail() {
  FakeWifi io; io.tlsFails=true; WifiHttpsTransport t(io,settings());
  TransportOutcome out;
  CHECK(t.start(1,reinterpret_cast<const uint8_t *>("42"),2,0));
  for (unsigned now=0; now<50 && t.step(now,out)!=TransportPhase::Done; ++now) {}
  CHECK(out.kind==TransportOutcome::Kind::ConnectionFailed && strcmp(out.detail,"tls:-0x2700:0x4")==0);
  io.failTls=false; CHECK(t.start(2,reinterpret_cast<const uint8_t *>("42"),2,100));
  for (unsigned now=100; now<150 && t.step(now,out)!=TransportPhase::Done; ++now) {}
  CHECK(strcmp(out.detail,"tcp")==0);
  FakeWifi dnsIo; dnsIo.dnsFails=true; WifiHttpsTransport d(dnsIo,settings());
  CHECK(d.start(3,reinterpret_cast<const uint8_t *>("42"),2,0));
  for (unsigned now=0; now<50 && d.step(now,out)!=TransportPhase::Done; ++now) {}
  CHECK(strcmp(out.detail,"dns")==0);
}
int main() {
  FakeWifi idleIo; WifiHttpsTransport idle(idleIo,settings());
  idle.service(0); CHECK(idleIo.starts==1); // connect even before first record
  idleIo.linked=false; idle.service(100); CHECK(idleIo.starts==1);
  idle.service(30000); CHECK(idleIo.starts==2);
  test_configuration();test_link_status();test_failure_detail();test_partial_io();test_bad_responses();test_failures_and_cancel();
  if(failures) return 1;
  puts("Wi-Fi HTTPS adapter tests pass (fake radio/DNS/TLS I/O; no network)");
}
