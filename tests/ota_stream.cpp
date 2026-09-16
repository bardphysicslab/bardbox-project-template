#include "BardBoxOTAStream.h"
#include <cassert>
#include <cstring>
#include <string>
struct Source {
 std::string bytes="firmware";size_t pos=0,maxRead=3;uint32_t time=0,step=1;
 bool online=true,stalled=false,error=false;
 int available(){return stalled?0:static_cast<int>(bytes.size()-pos);}
 int read(uint8_t*out,size_t count){if(error)return -1;count=std::min(count,maxRead);memcpy(out,bytes.data()+pos,count);pos+=count;return count;}
 bool connected(){return online;}
 uint32_t now(){return time;}
 void pause(){time+=step;}
};
int main(){
 auto accept=[](const uint8_t*,size_t){return true;};
 Source source;std::string output;
 assert(bardbox::receiveOTABytes(source,8,10,30,[&](const uint8_t*b,size_t n){output.append(reinterpret_cast<const char*>(b),n);return true;}));
 assert(output=="firmware");
 source=Source();source.time=UINT32_MAX-1;
 assert(bardbox::receiveOTABytes(source,8,10,30,accept));
 source=Source();source.stalled=true;
 assert(!bardbox::receiveOTABytes(source,8,3,30,accept));assert(source.time==3);
 source=Source();source.maxRead=1;
 assert(!bardbox::receiveOTABytes(source,8,3,4,accept));assert(source.time==4);
 source=Source();source.error=true;assert(!bardbox::receiveOTABytes(source,8,10,30,accept));
 source=Source();assert(!bardbox::receiveOTABytes(source,8,10,30,[](const uint8_t*,size_t){return false;}));
 source=Source();source.bytes="short";source.online=false;
 assert(!bardbox::receiveOTABytes(source,8,10,30,accept));
 source=Source();assert(bardbox::receiveOTABytes(source,3,10,30,accept));assert(source.pos==3);
 source=Source();assert(!bardbox::receiveOTABytes(source,0,10,30,accept));
 source=Source();source.bytes=std::string(3000,'x');source.maxRead=3000;
 assert(bardbox::receiveOTABytes(source,3000,10,30,[](const uint8_t*,size_t n){assert(n<=1024);return true;}));
}
