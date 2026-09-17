"""Actual NVS adapter against explicit storage/crypto API doubles, not hardware."""
from pathlib import Path
import os
import shutil
import subprocess


def test_first_write_readback_and_uncertain_retry(tmp_path):
    root = Path(__file__).resolve().parents[1]
    (tmp_path/'Preferences.h').write_text(r'''
#pragma once
#include <string>
#include <cstring>
namespace fake {static std::string bytes; static bool exists=false,read=true,write=true,key=true,ca=true;}
class Preferences {public:
bool begin(const char*,bool){return true;}
bool isKey(const char*){return fake::exists;}
size_t getBytesLength(const char*){return fake::bytes.size();}
size_t getBytes(const char*,void*out,size_t n){if(!fake::read)return 0;memcpy(out,fake::bytes.data(),n);return n;}
size_t putBytes(const char*,const void*in,size_t n){if(!fake::write)return 0;fake::exists=true;fake::bytes.assign((const char*)in,n);return n;}
};
''')
    (tmp_path/'mbedtls').mkdir()
    (tmp_path/'mbedtls/pk.h').write_text(r'''
#pragma once
#include "Preferences.h"
const int MBEDTLS_PK_ECDSA=1,MBEDTLS_ECP_DP_SECP256R1=1;
struct Group{int id=1;};struct EC{Group grp;};struct mbedtls_pk_context{EC ec;};
inline void mbedtls_pk_init(mbedtls_pk_context*){}
inline void mbedtls_pk_free(mbedtls_pk_context*){}
inline int mbedtls_pk_parse_public_key(mbedtls_pk_context*,const unsigned char*,size_t){return fake::key?0:-1;}
inline bool mbedtls_pk_can_do(mbedtls_pk_context*,int){return true;}
#define mbedtls_pk_ec(key) (&(key).ec)
''')
    (tmp_path/'mbedtls/x509_crt.h').write_text(r'''
#pragma once
struct Raw{unsigned len=1;};struct mbedtls_x509_crt{Raw raw;};
inline void mbedtls_x509_crt_init(mbedtls_x509_crt*){}
inline void mbedtls_x509_crt_free(mbedtls_x509_crt*){}
inline int mbedtls_x509_crt_parse(mbedtls_x509_crt*,const unsigned char*,size_t){return fake::ca?0:-1;}
''')
    source = tmp_path/'store.cpp'
    source.write_text(r'''
#include "BardBoxDeviceConfigStoreESP32.h"
#include <cassert>
using namespace bardbox;
int main(){
DeviceConfig c,out;c.uid="bb-ces-air-001";c.name="CESH";c.wifiSsid="lab";
c.telemetryUrl="https://lab.example/readings";c.ota.origin="https://lab.example";
c.ota.deviceToken=std::string(32,'t');c.ota.caPem="test CA";
c.signingKeyId="lab";c.signingPublicPem="test key";c.initialHash=std::string(64,'a');c.initialBytes=1000;
DeviceConfigStoreESP32 store;assert(store.begin());assert(!store.load(out));
fake::key=false;assert(!store.provision(c));assert(!fake::exists);fake::key=true;
fake::ca=false;assert(!store.provision(c));assert(!fake::exists);fake::ca=true;
fake::write=false;assert(!store.provision(c));assert(!fake::exists);fake::write=true;
fake::read=false;assert(!store.provision(c));assert(fake::exists); // committed but readback uncertain
fake::read=true;assert(store.provision(c));assert(store.load(out));assert(out.uid==c.uid);
auto original=fake::bytes;c.uid="bb-ces-air-002";assert(!store.provision(c));assert(fake::bytes==original);
fake::bytes[8]^=1;assert(!store.load(out));assert(!store.provision(c));assert(fake::exists);
}
''')
    binary = tmp_path/'store'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(tmp_path),
                    '-I', str(root/'software/firmware/include'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30,
                   env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
