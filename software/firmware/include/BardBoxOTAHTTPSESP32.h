#pragma once
#include "BardBoxOTAHTTPSConfig.h"
#include "BardBoxOTAWriterESP32.h"
#include "BardBoxOTAStream.h"
#include "BardBoxOTAStatus.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace bardbox {
// Single-worker adapter. Never call from the acquisition task. Verified TLS only;
// no redirects, no unbounded body allocation, no automatic restart.
class OTAHTTPSESP32 {
public:
    enum class Poll { Available, None, Failed };
    enum class Install { ReadyForReboot, AlreadyCurrent, Failed };
    enum class Report { Accepted, RetryLater, Rejected };
    static Report report(const OTAHTTPSConfig &config,const OTAStatusEvent &event) {
        if(event.body().empty()) return Report::Rejected;
        WiFiClientSecure client;
        HTTPClient http;
        if(!open(config,"/ota/v1/device/status",client,http)) return Report::RetryLater;
        http.addHeader("Content-Type","application/json");
        int code=http.POST(event.body().c_str());
        http.end();
        if(code==200) return Report::Accepted;
        // 4xx indicates credentials, stale assignment or malformed/conflicting
        // event. Refresh state/config; never hot-loop the same rejected report.
        if(code>=400 && code<500 && code!=408 && code!=429) return Report::Rejected;
        return Report::RetryLater;
    }
    static Poll poll(const OTAHTTPSConfig &config,OTAAssignment &assignment) {
        WiFiClientSecure client;
        HTTPClient http;
        if(!open(config,"/ota/v1/device/assignment",client,http)) return Poll::Failed;
        int code=http.GET();
        if(code==204) {http.end();return Poll::None;}
        int length=http.getSize();
        if(code!=200 || length<=0 || length>4096 || !plain(http)) {http.end();return Poll::Failed;}
        std::string body;
        body.reserve(length);
        bool read=receive(http,length,config.readMs,config.transferMs,[&](const uint8_t *bytes,size_t count) {
            body.append(reinterpret_cast<const char *>(bytes),count); return true;
        });
        http.end();
        return read && OTAAssignmentParser::parse(body,assignment) ? Poll::Available : Poll::Failed;
    }
    template<class Persist>
    static Install install(const OTAHTTPSConfig &config,const OTAAssignment &assignment,
            const OTATarget &target,const std::string &keyId,const std::string &publicPem,
            OTAStateMachine &state,const std::string &runningHash,uint32_t runningBytes,Persist persist) {
        if(assignment.artifactPath!="/ota/v1/device/artifact/"+assignment.manifest.releaseId ||
           verifyOTAESP32(assignment.manifest,target,assignment.signature,keyId,publicPem)!=OTAVerification::Valid)
            return Install::Failed;
        WiFiClientSecure client;
        HTTPClient http;
        if(!open(config,assignment.artifactPath,client,http)) return Install::Failed;
        int code=http.GET();
        if(code!=200 || http.getSize()!=static_cast<int>(assignment.manifest.size) || !plain(http)) {
            http.end();return Install::Failed;
        }
        OTAWriterESP32 writer;
        auto begun=writer.begin(assignment,target,keyId,publicPem,state,runningHash,runningBytes,persist);
        if(begun==OTAWriterESP32::Start::AlreadyCurrent){http.end();return Install::AlreadyCurrent;}
        if(begun!=OTAWriterESP32::Start::Started){http.end();return Install::Failed;}
        bool read=receive(http,assignment.manifest.size,config.readMs,config.transferMs,
            [&](const uint8_t *bytes,size_t count){return writer.append(bytes,count);});
        http.end();
        if(!read){writer.abort();state.fail("download_transport_or_write_failed",persist);return Install::Failed;}
        return writer.finish(state,persist) ? Install::ReadyForReboot : Install::Failed;
    }
private:
    static bool open(const OTAHTTPSConfig &config,const std::string &path,WiFiClientSecure &client,HTTPClient &http) {
        if(!config.valid()) return false;
        client.setCACert(config.caPem.c_str());
        client.setHandshakeTimeout((config.connectMs+999)/1000);
        http.setConnectTimeout(config.connectMs);
        http.setTimeout(config.readMs);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if(!http.begin(client,(config.origin+path).c_str())) return false;
        const char *headers[]={"Content-Encoding"};
        http.collectHeaders(headers,1);
        http.addHeader("Authorization",("Bearer "+config.deviceToken).c_str());
        http.addHeader("Accept-Encoding","identity");
        return true;
    }
    static bool plain(HTTPClient &http) {
        String encoding=http.header("Content-Encoding");
        return !encoding.length() || encoding=="identity";
    }
    template<class Consume>
    static bool receive(HTTPClient &http,uint32_t expected,uint32_t idleMs,uint32_t budgetMs,Consume consume) {
        WiFiClient *stream=http.getStreamPtr();
        if(!stream) return false;
        struct Source {
            WiFiClient *stream; HTTPClient &http;
            int available(){return stream->available();}
            int read(uint8_t *bytes,size_t count){return stream->read(bytes,count);}
            bool connected(){return http.connected();}
            uint32_t now(){return millis();}
            void pause(){delay(1);}
        } source{stream,http};
        return receiveOTABytes(source,expected,idleMs,budgetMs,consume);
    }
};
} // namespace bardbox
