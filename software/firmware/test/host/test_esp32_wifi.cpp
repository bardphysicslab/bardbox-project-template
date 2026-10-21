// Tests the actual ESP32 provider against stubbed radio, DNS and TLS calls.
// Not a TLS handshake, radio or hardware test.
#include "BardBoxEsp32Wifi.h"
#include "BardBoxEsp32Sntp.h"
#include <WiFi.h>
#include <lwip/dns.h>
#include <esp_tls.h>
#include <assert.h>
#include <stdio.h>
#include <string>
StubWifi WiFi;
dns_cb pendingDns=nullptr;
void *pendingDnsArg=nullptr;
int tlsCreated=0,tlsDestroyed=0,tlsPendingChecks=0;
int tlsFailLast=0,tlsFailCode=0,tlsFailFlags=0;
int g_configTimeCalls=0;
void (*g_sntpCallback)(struct timeval *)=nullptr;
bool tlsFailNow=false;
std::string tlsSent;
size_t tlsReadAt=0;
static void dnsDone() {
 assert(pendingDns);
 const ip_addr_t addr={1};
 auto cb=pendingDns;pendingDns=nullptr;cb("receiver.invalid",&addr,pendingDnsArg);
}
int main() {
 auto &t=bbcore::defaultEsp32WifiTransport(BARDBOX_WIFI_SSID,"");
 // One clock owner: TLS waits for the Time provider's first sync and never
 // starts SNTP itself (template#49).
 bbcore::TimeSyncProvider &sntp=bbcore::defaultEsp32SntpProvider();
 int64_t utc=0;
 assert(!sntp.utcNow(utc) && sntp.syncCount()==0);
 sntp.start("pool.ntp.org",3600000); assert(g_configTimeCalls==1 && g_sntpCallback);
 g_sntpCallback(nullptr); assert(sntp.utcNow(utc) && sntp.syncCount()==1 && utc>1704067200000LL);
 assert(t.configured());
 auto body=reinterpret_cast<const uint8_t *>("42");
 assert(t.start(1,body,2,0));
 bbcore::TransportOutcome out;
 t.step(0,out);t.step(1,out); // queued resolver, waiting for callback
 assert(pendingDns);
 t.abort();
 assert(!t.start(2,body,2,2)); // callback storage cannot be reused
 dnsDone();
 assert(t.start(3,body,2,3));
 for(unsigned now=4;now<100;++now) {
   if(t.step(now,out)==bbcore::TransportPhase::Done) break;
   if(pendingDns) dnsDone();
 }
 assert(out.kind==bbcore::TransportOutcome::Kind::HttpStatus && out.httpStatus==204);
 assert(out.attemptId==3 && out.endpointVerified);
 assert(tlsCreated==1 && tlsDestroyed==1 && tlsPendingChecks==2);
 assert(tlsSent.find("POST /test HTTP/1.1\r\nHost: receiver.invalid\r\n")==0);
 bbcore::ConnectivityStatus link; t.linkStatus(link);
 assert(link.wifi==bbcore::WifiState::Connected && std::string(link.ip)=="192.0.2.10" && link.rssiKnown && link.rssiDbm==-61);
 assert(std::string(link.mac)=="02:00:5e:10:20:30" && std::string(link.receiver)=="https://receiver.invalid");
 // One call: CONNECTING -> HANDSHAKE -> FAIL. Classified from the recorded
 // error, not from the final state (Codex review of 217022a).
 tlsFailNow=true; tlsFailLast=ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED; tlsFailCode=0x2700; tlsFailFlags=0x4;
 assert(t.start(4,body,2,200));
 for(unsigned now=200;now<300;++now) { if(t.step(now,out)==bbcore::TransportPhase::Done) break; if(pendingDns) dnsDone(); }
 assert(out.kind==bbcore::TransportOutcome::Kind::ConnectionFailed && std::string(out.detail)=="tls:-0x2700:0x4");
 tlsFailNow=true; tlsFailLast=ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST; tlsFailCode=0; tlsFailFlags=0;
 assert(t.start(5,body,2,300));
 for(unsigned now=300;now<400;++now) { if(t.step(now,out)==bbcore::TransportPhase::Done) break; if(pendingDns) dnsDone(); }
 assert(std::string(out.detail)=="tcp:0x8004");
 tlsFailNow=true; tlsFailLast=0;
 assert(t.start(6,body,2,400));
 for(unsigned now=400;now<500;++now) { if(t.step(now,out)==bbcore::TransportPhase::Done) break; if(pendingDns) dnsDone(); }
 assert(std::string(out.detail)=="connect:0x0");
 WiFi.up=false; bbcore::ConnectivityStatus down; t.linkStatus(down);
 assert(down.wifi==bbcore::WifiState::Disconnected && !down.rssiKnown && down.ip[0]==0);
 puts("ESP32 provider tests pass (stubbed APIs, including pending DNS and select rearming)");
}
