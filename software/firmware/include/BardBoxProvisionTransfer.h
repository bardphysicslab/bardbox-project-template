#pragma once
#include "BardBoxDeviceConfig.h"

namespace bardbox {
// Bounded, ordered USB staging. Hex keeps commands under a 64-byte serial limit.
// No partial record reaches NVS. Timeouts do not erase existing configuration.
class ProvisionTransfer {
public:
    bool begin(size_t expected,uint32_t now) {
        cancel();
        if(expected<35 || expected>DeviceConfigRecord::MaxBytes) return false;
        expected_=expected; started_=last_=now; active_=true;
        bytes_.reserve(expected); return true;
    }
    bool append(size_t offset,const std::string &hex,uint32_t now) {
        if(!alive(now) || hex.empty() || hex.size()>32 || hex.size()%2) return false;
        std::string part;
        for(size_t i=0;i<hex.size();i+=2) {
            int high=digit(hex[i]),low=digit(hex[i+1]);
            if(high<0 || low<0) return false;
            part.push_back(static_cast<char>((high<<4)|low));
        }
        if(offset>bytes_.size() || part.size()>expected_-offset) return false;
        if(offset<bytes_.size()) {
            if(part.size()>bytes_.size()-offset || bytes_.compare(offset,part.size(),part)) return false;
        } else bytes_+=part;
        last_=now; return true;
    }
    bool decode(DeviceConfig &config,uint32_t now) {
        return alive(now) && bytes_.size()==expected_ && DeviceConfigRecord::decode(bytes_,config);
    }
    size_t received() const { return bytes_.size(); }
    bool alive(uint32_t now) {
        if(active_ && (static_cast<uint32_t>(now-last_)>30000 ||
                       static_cast<uint32_t>(now-started_)>180000)) cancel();
        return active_;
    }
    void cancel() { bytes_.clear(); expected_=0; active_=false; }
private:
    static int digit(char c) {
        if(c>='0'&&c<='9') return c-'0';
        if(c>='a'&&c<='f') return c-'a'+10;
        return -1;
    }
    std::string bytes_;
    size_t expected_=0;
    uint32_t started_=0,last_=0;
    bool active_=false;
};
} // namespace bardbox
