#include "BardBoxOTAHTTPSConfig.h"
#include <cassert>
using bardbox::OTAHTTPSConfig;
int main(){
 OTAHTTPSConfig config;config.origin="https://lab.example";
 config.deviceToken=std::string(32,'a');config.caPem="test CA placeholder";
 assert(config.valid());
 for(const char *origin:{"http://lab.example","https://","https://user@lab.example", "https://lab.example/path",
     "https://lab.example?x=1","https://lab.example#fragment","https://lab.example:0","https://lab.example:65536",
     "https://lab.example:","https://lab.example:443:5","https://lab.example\r\nX:yes"}){
  auto bad=config;bad.origin=origin;assert(!bad.valid());
 }
 config.origin="https://lab.example:443";assert(config.valid());
 for(auto token:{std::string(),std::string(31,'a'),std::string(257,'a'),std::string(32,'a')+"\r\nX:y"}){
  auto bad=config;bad.deviceToken=token;assert(!bad.valid());
 }
 auto bad=config;bad.caPem="";assert(!bad.valid());
 bad=config;bad.caPem=std::string("a\0b",3);assert(!bad.valid());
 bad=config;bad.readMs=0;assert(!bad.valid());
 bad=config;bad.transferMs=600001;assert(!bad.valid());
}
