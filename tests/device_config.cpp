#include "BardBoxDeviceConfig.h"
#include "BardBoxProvisionTransfer.h"
#include <cassert>
using namespace bardbox;
static DeviceConfig fixture() {
    DeviceConfig c; c.uid="bb-ces-air-001"; c.name="CESH Air 001"; c.wifiSsid="Lab WiFi";
    c.wifiPassword="password"; c.telemetryUrl="https://lab.example/api/v1/readings";
    c.ota.origin="https://lab.example"; c.ota.deviceToken=std::string(32,'t');
    c.ota.caPem="TEST CA\n"; c.signingPublicPem="TEST KEY\n"; c.signingKeyId="lab";
    c.initialHash=std::string(64,'a'); c.initialBytes=1000; return c;
}
int main() {
    DeviceConfig c=fixture(),out;
    std::string bytes;
    assert(DeviceConfigRecord::encode(c,bytes));
    assert(DeviceConfigRecord::decode(bytes,out));
    assert(out.uid==c.uid && out.wifiPassword==c.wifiPassword && out.ota.caPem==c.ota.caPem);
    for(size_t i=0;i<bytes.size();++i) {
        std::string damaged=bytes; damaged[i]^=1;
        assert(!DeviceConfigRecord::decode(damaged,out));
        assert(!DeviceConfigRecord::decode(bytes.substr(0,i),out));
    }
    c.wifiPassword=""; assert(c.valid());
    c.wifiPassword="short"; assert(!c.valid());
    c=fixture(); c.telemetryUrl="http://lab.example/readings"; assert(!c.valid());
    c=fixture(); c.telemetryToken="secret\r\nInjected"; assert(!c.valid());
    c=fixture(); c.wifiSsid=std::string(33,'a'); assert(!c.valid());
    c=fixture(); c.initialBytes=0; assert(!c.valid());
    c=fixture(); c.ota.deviceToken="bad token"; assert(!c.valid());
    ProvisionTransfer transfer;
    const uint32_t start=UINT32_MAX-100;
    assert(!transfer.begin(8193,start));
    assert(transfer.begin(bytes.size(),start));
    const char *digits="0123456789abcdef";
    for(size_t offset=0;offset<bytes.size();offset+=16) {
        std::string hex;
        for(size_t j=offset;j<bytes.size()&&j<offset+16;++j) {
            unsigned char b=bytes[j]; hex+=digits[b>>4]; hex+=digits[b&15];
        }
        assert(!transfer.append(offset+1,hex,start+offset));
        assert(transfer.append(offset,hex,start+offset));
        assert(transfer.append(offset,hex,start+offset)); // Lost ACK replay.
        assert(!transfer.append(offset,"zz",start+offset));
    }
    assert(transfer.decode(out,start+bytes.size()));
    assert(out.uid==fixture().uid);
    assert(!transfer.alive(start+bytes.size()+30001));
    assert(!transfer.decode(out,start+bytes.size()+30001));
    assert(transfer.begin(bytes.size(),0));
    for(uint32_t now=20000;now<=180000;now+=20000) assert(transfer.append(0,"44",now));
    assert(!transfer.append(0,"44",180001)); // Duplicate ACKs cannot extend total budget.
}
