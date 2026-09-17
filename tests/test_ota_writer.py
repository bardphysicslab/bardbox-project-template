"""Test production writer ordering/failures against an ESP API double.

Cryptography and physical flash behavior are deliberately not simulated here.
The real ESP SDK is validated separately by the PlatformIO compile fixture.
"""
import os
from pathlib import Path
import shutil
import subprocess


def test_inactive_writer_failure_ordering(tmp_path):
    root = Path(__file__).resolve().parents[1]
    sdk = tmp_path/'FakeSDK.h'
    sdk.write_text(r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <string>
#include <vector>
using esp_err_t=int; using esp_ota_handle_t=unsigned;
const int ESP_OK=0, ESP_PARTITION_TYPE_APP=0;
const int MBEDTLS_PK_ECDSA=1, MBEDTLS_ECP_DP_SECP256R1=1, MBEDTLS_MD_SHA256=1;
struct esp_partition_t {unsigned address,size;int type;};
namespace fake {
static bool signature=true,write=true,end=true,select=true,hash=true;
static std::vector<std::string> events;
static esp_partition_t running={0x10000,2097152,0},next={0x210000,2097152,0};
static void reset(){signature=write=end=select=hash=true;events.clear();next.address=0x210000;}
}
struct mbedtls_sha256_context {};
struct Group {int id=1;}; struct EC {Group grp;}; struct mbedtls_pk_context {EC ec;};
static void mbedtls_sha256_init(mbedtls_sha256_context*){}
static void mbedtls_sha256_free(mbedtls_sha256_context*){}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*,int){return 0;}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*,const unsigned char*,size_t){return 0;}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*,unsigned char*out){memset(out,fake::hash?0:1,32);return 0;}
static int mbedtls_sha256_ret(const unsigned char*,size_t,unsigned char*out,int){memset(out,0,32);return 0;}
static int mbedtls_base64_decode(unsigned char*,size_t,size_t*n,const unsigned char*,size_t){*n=1;return 0;}
static void mbedtls_pk_init(mbedtls_pk_context*){}
static void mbedtls_pk_free(mbedtls_pk_context*){}
static int mbedtls_pk_parse_public_key(mbedtls_pk_context*,const unsigned char*,size_t){return 0;}
static bool mbedtls_pk_can_do(mbedtls_pk_context*,int){return true;}
#define mbedtls_pk_ec(key) (&(key).ec)
static int mbedtls_pk_verify(mbedtls_pk_context*,int,const unsigned char*,size_t,const unsigned char*,size_t){return fake::signature?0:-1;}
static const esp_partition_t*esp_ota_get_running_partition(){return &fake::running;}
static const esp_partition_t*esp_ota_get_next_update_partition(const void*){return &fake::next;}
static int esp_ota_begin(const esp_partition_t*p,size_t,esp_ota_handle_t*h){if(p==&fake::running)return -1;fake::events.push_back("begin");*h=1;return 0;}
static int esp_ota_write(esp_ota_handle_t,const void*,size_t){fake::events.push_back("write");return fake::write?0:-1;}
static int esp_ota_end(esp_ota_handle_t){fake::events.push_back("end");return fake::end?0:-1;}
static int esp_ota_abort(esp_ota_handle_t){fake::events.push_back("abort");return 0;}
static int esp_ota_set_boot_partition(const esp_partition_t*){fake::events.push_back("select");return fake::select?0:-1;}
''')
    for name in ['esp_ota_ops.h','mbedtls/base64.h','mbedtls/pk.h','mbedtls/sha256.h']:
        target=tmp_path/name; target.parent.mkdir(exist_ok=True)
        target.write_text('#include "FakeSDK.h"\n')
    source=tmp_path/'writer.cpp'
    source.write_text(r'''
#include "BardBoxOTAWriterESP32.h"
#include <cassert>
#include <algorithm>
using namespace bardbox;
int main(){
 OTAAssignment a; a.generation=1;a.signature="YQ==";
 auto&m=a.manifest;m.format="bardbox-ota-v1";m.releaseId="r1";m.component="app";
 m.version="0.7.0";m.target="s3";m.layout="dual";m.configSchema="1";m.queueSchema="bq1";
 m.sha256=std::string(64,'0');m.keyId="lab";m.size=4;
 OTATarget t;t.component=m.component;t.target=m.target;t.layout=m.layout;
 t.configSchema=m.configSchema;t.queueSchema=m.queueSchema;t.slotBytes=2097152;
 uint8_t bytes[5]={};
 for(int scenario=0;scenario<11;++scenario){
  fake::reset();OTAStateMachine state;assert(state.restore(OTAState()));
  bool failPending=scenario==6;
  auto save=[&](const OTAState&s){fake::events.push_back("save"+std::to_string(static_cast<int>(s.phase)));return !(failPending&&s.phase==OTAPhase::PendingBoot);};
  if(scenario==1)fake::signature=false;
  if(scenario==2)fake::next.address=fake::running.address;
  OTAWriterESP32 writer;
  auto start=writer.begin(a,t,"lab","test-public-key",state,std::string(64,'b'),4,save);
  if(scenario==1||scenario==2){assert(start==OTAWriterESP32::Start::Rejected);assert(fake::events.empty());continue;}
  assert(start==OTAWriterESP32::Start::Started);
  assert(fake::events[0]=="save1"&&fake::events[1]=="begin");
  if(scenario==3){assert(!writer.append(bytes,5));assert(fake::events.back()=="abort");continue;}
  if(scenario==4)fake::write=false;
  bool wrote=writer.append(bytes,scenario==8?3:4);
  if(scenario==4){assert(!wrote);assert(fake::events.back()=="abort");continue;}
  assert(wrote);
  if(scenario==5)fake::hash=false;
  if(scenario==7)fake::end=false;
  if(scenario==9)fake::select=false;
  bool finished=writer.finish(state,save,scenario!=10);
  if(scenario==0){
   assert(finished&&writer.readyForReboot());
   assert((fake::events==std::vector<std::string>{"save1","begin","write","end","save2","select"}));
  }else if(scenario==10){
   assert(finished&&!writer.readyForReboot());
   assert(state.state().phase==OTAPhase::PendingBoot);
   assert((fake::events==std::vector<std::string>{"save1","begin","write","end","save2"}));
  }else if(scenario==9){
   assert(!finished&&!writer.readyForReboot());
   assert(state.state().phase==OTAPhase::Failed);
   assert(fake::events[fake::events.size()-2]=="select");
  }else{
   assert(!finished&&!writer.readyForReboot());
   assert(std::find(fake::events.begin(),fake::events.end(),"select")==fake::events.end());
  }
 }
}
''')
    binary=tmp_path/'writer'
    subprocess.run([shutil.which('c++'),'-std=c++11','-fsanitize=address,undefined',
                    '-I',str(tmp_path),'-I',str(root/'software/firmware/include'),str(source),
                    '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
