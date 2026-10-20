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
    "HTTP/1.1 200 OK\r\nBad Header\r\n\r\n", "HTTP/1.1 200 OK\rX"};
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
int main() {
  FakeWifi idleIo; WifiHttpsTransport idle(idleIo,settings());
  idle.service(0); CHECK(idleIo.starts==1); // connect even before first record
  idleIo.linked=false; idle.service(100); CHECK(idleIo.starts==1);
  idle.service(30000); CHECK(idleIo.starts==2);
  test_configuration();test_partial_io();test_bad_responses();test_failures_and_cancel();
  if(failures) return 1;
  puts("Wi-Fi HTTPS adapter tests pass (fake radio/DNS/TLS I/O; no network)");
}
