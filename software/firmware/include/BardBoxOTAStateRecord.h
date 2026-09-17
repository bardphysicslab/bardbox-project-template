#pragma once
#include "BardBoxOTAState.h"

namespace bardbox {
class OTAStateRecord {
public:
    static const size_t MaxBytes=512;
    static bool encode(const OTAState &state, std::string &out) {
        out.clear();
        if(!OTAStateMachine::valid(state)) return false;
        out="OS2"; out.push_back(static_cast<char>(state.phase));
        put32(out,state.generation); put32(out,state.sequence);
        put32(out,state.candidateBytes); put32(out,state.previousBytes);
        for(const auto *field:{&state.releaseId,&state.candidateHash,&state.previousHash,
                               &state.version,&state.failure}) {
            out.push_back(static_cast<char>(field->size())); out+=*field;
        }
        put32(out,crc(out));
        return true;
    }
    static bool decode(const std::string &bytes, OTAState &state) {
        if(bytes.size()<29 || bytes.size()>MaxBytes || bytes.compare(0,3,"OS2")!=0 ||
           get32(bytes,bytes.size()-4)!=crc(bytes.substr(0,bytes.size()-4))) return false;
        OTAState candidate;
        candidate.phase=static_cast<OTAPhase>(static_cast<unsigned char>(bytes[3]));
        candidate.generation=get32(bytes,4); candidate.sequence=get32(bytes,8);
        candidate.candidateBytes=get32(bytes,12); candidate.previousBytes=get32(bytes,16);
        size_t pos=20, end=bytes.size()-4;
        for(auto *field:{&candidate.releaseId,&candidate.candidateHash,&candidate.previousHash,
                         &candidate.version,&candidate.failure}) {
            if(pos>=end) return false;
            size_t size=static_cast<unsigned char>(bytes[pos++]);
            if(size>end-pos) return false;
            *field=bytes.substr(pos,size); pos+=size;
        }
        if(pos!=end || !OTAStateMachine::valid(candidate)) return false;
        state=candidate;
        return true;
    }
private:
    static void put32(std::string &out,uint32_t n) {
        for(unsigned i=0;i<4;++i) out.push_back(static_cast<char>(n>>(8*i)));
    }
    static uint32_t get32(const std::string &s,size_t p) {
        uint32_t n=0;
        for(unsigned i=0;i<4;++i) n|=static_cast<uint32_t>(static_cast<unsigned char>(s[p+i]))<<(8*i);
        return n;
    }
    static uint32_t crc(const std::string &s) {
        uint32_t n=0xffffffffU;
        for(unsigned char c:s) {
            n^=c;
            for(unsigned bit=0;bit<8;++bit) n=(n>>1)^(0xedb88320U & (0U-(n&1U)));
        }
        return ~n;
    }
};
} // namespace bardbox
