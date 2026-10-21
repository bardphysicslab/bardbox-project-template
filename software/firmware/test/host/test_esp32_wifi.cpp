// Tests the actual ESP32 provider against stubbed radio, DNS and TLS calls.
// Not a TLS handshake, radio or hardware test.
#include "BardBoxEsp32Wifi.h"
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
std::string tlsSent;
size_t tlsReadAt=0;
static void dnsDone() {
 assert(pendingDns);
 const ip_addr_t addr={1};
 auto cb=pendingDns;pendingDns=nullptr;cb("receiver.invalid",&addr,pendingDnsArg);
}
int main() {
 auto &t=bbcore::defaultEsp32WifiTransport(BARDBOX_WIFI_SSID,"");
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
 WiFi.up=false; bbcore::ConnectivityStatus down; t.linkStatus(down);
 assert(down.wifi==bbcore::WifiState::Disconnected && !down.rssiKnown && down.ip[0]==0);
 puts("ESP32 provider tests pass (stubbed APIs, including pending DNS and select rearming)");
}
