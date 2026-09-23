#pragma once
#include <stdint.h>
#include <stddef.h>

namespace bardbox {
// Source supplies available/read/connected/now/pause. Consumer returns false on
// write failure. Clock arithmetic tolerates uint32 millisecond wraparound.
template<class Source,class Consume>
bool receiveOTABytes(Source &source,uint32_t expected,uint32_t idleMs,uint32_t budgetMs,Consume consume) {
    if(!expected || expected>2097152 || !idleMs || !budgetMs || budgetMs>600000) return false;
    uint32_t start=source.now(),last=start,received=0;
    uint8_t buffer[1024];
    while(received<expected) {
        uint32_t now=source.now();
        if(now-start>=budgetMs || now-last>=idleMs) return false;
        int available=source.available();
        if(available<0) return false;
        if(available>0) {
            size_t count=static_cast<size_t>(available);
            if(count>sizeof(buffer))count=sizeof(buffer);
            if(count>expected-received)count=expected-received;
            int read=source.read(buffer,count);
            if(read<0 || static_cast<size_t>(read)>count)return false;
            if(read>0){if(!consume(buffer,read))return false;received+=read;last=source.now();}
        }else if(!source.connected())return false;
        source.pause();
    }
    return true;
}
} // namespace bardbox
