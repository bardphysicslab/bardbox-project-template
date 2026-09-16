#pragma once
#include "BardBoxOTAHTTPSConfig.h"
#include "BardBoxOTAState.h"

namespace bardbox {
// Initial provisioning schema. Per-device secrets never belong in release bins.
struct DeviceConfig {
    std::string uid, name, wifiSsid, wifiPassword;
    std::string telemetryUrl, telemetryToken;
    OTAHTTPSConfig ota;
    std::string signingKeyId, signingPublicPem, initialHash;
    uint32_t initialBytes=0;
    bool valid() const {
        if(!otaToken(uid) || uid.size()>48 || name.empty() || name.size()>64 || !text(name) || name.find(',')!=std::string::npos || wifiSsid.empty() || wifiSsid.size()>32 ||
           wifiPassword.size()>63 || (!wifiPassword.empty() && wifiPassword.size()<8) ||
           !text(wifiSsid) || !text(wifiPassword) || !ota.valid() ||
           ota.connectMs!=10000 || ota.readMs!=5000 || ota.transferMs!=300000 ||
           !otaToken(signingKeyId) || signingPublicPem.empty() || signingPublicPem.size()>1024 ||
           !text(signingPublicPem,true) || !OTAStateMachine::digest(initialHash) ||
           !initialBytes || initialBytes>2097152 || telemetryUrl.size()>512 ||
           telemetryToken.size()>256 || !text(telemetryToken)) return false;
        // Validate the telemetry origin using the same HTTPS-only policy; path is
        // permitted but not fragments, userinfo, whitespace or control characters.
        auto slash=telemetryUrl.find('/',8);
        OTAHTTPSConfig endpoint=ota;
        endpoint.origin=telemetryUrl.substr(0,slash);
        if(!endpoint.valid() || !text(telemetryUrl) || telemetryUrl.find('#')!=std::string::npos || telemetryUrl.find('?')!=std::string::npos) return false;
        for(unsigned char c:telemetryUrl) if(c<=32 || c>=127) return false;
        return true;
    }
    static bool text(const std::string &s,bool multiline=false) {
        for(unsigned char c:s) if(c==0 || c==127 || (c<32 && !(multiline && (c=='\n'||c=='\r')))) return false;
        return true;
    }
};

class DeviceConfigRecord {
public:
    static const size_t MaxBytes=8192;
    static bool encode(const DeviceConfig &c,std::string &out) {
        out.clear(); if(!c.valid()) return false;
        out="DC1"; put32(out,c.initialBytes);
        // Transport deadlines are reference defaults, not provisioned policy.
        for(const auto *s:{&c.uid,&c.name,&c.wifiSsid,&c.wifiPassword,&c.telemetryUrl,&c.telemetryToken,
                          &c.ota.origin,&c.ota.deviceToken,&c.ota.caPem,&c.signingKeyId,
                          &c.signingPublicPem,&c.initialHash}) {
            out.push_back(static_cast<char>(s->size()));
            out.push_back(static_cast<char>(s->size()>>8)); out+=*s;
        }
        put32(out,crc(out)); return out.size()<=MaxBytes;
    }
    static bool decode(const std::string &in,DeviceConfig &out) {
        if(in.size()<35 || in.size()>MaxBytes || in.compare(0,3,"DC1") ||
           get32(in,in.size()-4)!=crc(in.substr(0,in.size()-4))) return false;
        DeviceConfig candidate; candidate.initialBytes=get32(in,3);
        size_t pos=7, end=in.size()-4;
        for(auto *s:{&candidate.uid,&candidate.name,&candidate.wifiSsid,&candidate.wifiPassword,&candidate.telemetryUrl,
                    &candidate.telemetryToken,&candidate.ota.origin,&candidate.ota.deviceToken,
                    &candidate.ota.caPem,&candidate.signingKeyId,&candidate.signingPublicPem,&candidate.initialHash}) {
            if(end-pos<2) return false;
            size_t n=static_cast<unsigned char>(in[pos])+(static_cast<unsigned char>(in[pos+1])<<8);
            pos+=2; if(n>end-pos) return false;
            *s=in.substr(pos,n); pos+=n;
        }
        if(pos!=end || !candidate.valid()) return false;
        out=candidate; return true;
    }
private:
    static void put32(std::string &out,uint32_t n) {
        for(unsigned i=0;i<4;++i) out.push_back(static_cast<char>(n>>(8*i)));
    }
    static uint32_t get32(const std::string &s,size_t pos) {
        uint32_t n=0;
        for(unsigned i=0;i<4;++i) n|=static_cast<uint32_t>(static_cast<unsigned char>(s[pos+i]))<<(8*i);
        return n;
    }
    static uint32_t crc(const std::string &s) {
        uint32_t n=0xffffffffU;
        for(unsigned char c:s) {
            n^=c; for(unsigned i=0;i<8;++i) n=(n>>1)^(0xedb88320U & (0U-(n&1U)));
        }
        return ~n;
    }
};
} // namespace bardbox
