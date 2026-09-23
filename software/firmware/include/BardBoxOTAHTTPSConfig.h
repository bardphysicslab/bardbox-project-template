#pragma once
#include <stdint.h>
#include <string>

namespace bardbox {
struct OTAHTTPSConfig {
    std::string origin, deviceToken, caPem;
    uint16_t connectMs=10000, readMs=5000;
    uint32_t transferMs=300000;
    bool valid() const {
        if(origin.compare(0,8,"https://") || origin.size()>300 || deviceToken.size()<32 ||
           deviceToken.size()>256 || caPem.empty() || caPem.size()>4096 ||
           caPem.find('\0')!=std::string::npos || !connectMs || connectMs>30000 ||
           !readMs || readMs>30000 || !transferMs || transferMs>600000) return false;
        std::string host=origin.substr(8);
        auto colon=host.find(':');
        if(colon!=std::string::npos) {
            std::string port=host.substr(colon+1);
            if(port.empty() || port.size()>5) return false;
            uint32_t number=0;
            for(char c:port) {if(c<'0'||c>'9')return false;number=number*10+(c-'0');}
            if(!number || number>65535) return false;
            host.resize(colon);
        }
        if(host.empty() || host.size()>253 || host.front()=='.' || host.back()=='.') return false;
        for(char c:host) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'))return false;
        for(char c:deviceToken) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
        return true;
    }
};
} // namespace bardbox
