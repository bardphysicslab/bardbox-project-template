#pragma once

// Versioned lossless fixed-dictionary record codec. No sensor-specific schema,
// floating-point conversions, heap-sized decompression or mutable dictionaries.
// Store a CRC-protected base record once per bounded chunk; every delta refers
// directly to that base, never to another delta. A damaged delta cannot corrupt
// later records. This is a codec only; durable framing/queue ACKs belong to storage.
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <algorithm>

namespace bardbox {
class RecordDelta {
public:
    static const size_t MaxRecord = 8192;

    static uint32_t crc32(const std::string &s) {
        uint32_t crc = 0xffffffffU;
        for (unsigned char ch : s) {
            crc ^= ch;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
        return ~crc;
    }

    explicit RecordDelta(const std::string &base) : base_(base) {
        if (base.size() > MaxRecord) { base_.clear(); return; }
        for (size_t i=0; i+2<base_.size(); ++i)
            index_[hash(base_,i)].push_back(static_cast<uint16_t>(i));
        if (!split(base_,parts_,values_)) { parts_.clear(); values_.clear(); }
        valid_ = true;
    }

    bool encode(const std::string &record, std::string &out) const {
        out.clear();
        if (!valid_ || record.size() > MaxRecord) return false;
        if (!values_.empty() && encodeFields(record,out)) return true;
        out = "BD1";
        put16(out, static_cast<uint16_t>(record.size()));
        put32(out, crc32(base_));
        put32(out, crc32(record));
        std::string literal;
        for (size_t i=0; i<record.size();) {
            size_t length=0, offset=0;
            match(record,i,length,offset);
            size_t digits=0;
            while (i+digits<record.size() && digits<255 && nibble(record[i+digits])>=0) ++digits;
            if (length>=4 && length>=digits) {
                flush(literal,out);
                out.push_back(static_cast<char>(128+length-3));
                put16(out,static_cast<uint16_t>(offset));
                i += length;
            } else if (digits>=6) {
                flush(literal,out);
                out.push_back(static_cast<char>(255));
                out.push_back(static_cast<char>(digits));
                for (size_t n=0;n<digits;n+=2) {
                    unsigned byte=static_cast<unsigned>(nibble(record[i+n]))<<4;
                    if (n+1<digits) byte|=static_cast<unsigned>(nibble(record[i+n+1]));
                    out.push_back(static_cast<char>(byte));
                }
                i += digits;
            } else {
                literal.push_back(record[i++]);
                if (literal.size()==128) flush(literal,out);
            }
        }
        flush(literal,out);
        return true;
    }

    bool decode(const std::string &encoded, std::string &record) const {
        record.clear();
        if (!valid_ || encoded.size()<13) return false;
        if (encoded.compare(0,3,"BS1")==0) return decodeFields(encoded,record);
        if (encoded.compare(0,3,"BD1")!=0) return false;
        size_t expected=get16(encoded,3);
        if (expected>MaxRecord || get32(encoded,5)!=crc32(base_)) return false;
        std::string candidate;
        candidate.reserve(expected);
        for (size_t i=13;i<encoded.size();) {
            unsigned tag=static_cast<unsigned char>(encoded[i++]);
            if (tag<128) {
                size_t length=tag+1;
                if (length>encoded.size()-i || length>expected-candidate.size()) return false;
                candidate.append(encoded,i,length); i+=length;
            } else if (tag<255) {
                if (encoded.size()-i<2) return false;
                size_t length=tag-128+3, offset=get16(encoded,i); i+=2;
                if (offset>base_.size() || length>base_.size()-offset || length>expected-candidate.size()) return false;
                candidate.append(base_,offset,length);
            } else {
                if (i==encoded.size()) return false;
                size_t length=static_cast<unsigned char>(encoded[i++]);
                size_t bytes=(length+1)/2;
                if (length<6 || bytes>encoded.size()-i || length>expected-candidate.size()) return false;
                for (size_t n=0;n<length;++n) {
                    unsigned byte=static_cast<unsigned char>(encoded[i+n/2]);
                    candidate.push_back(alphabet()[(n%2)?(byte&15):(byte>>4)]);
                }
                i+=bytes;
            }
        }
        if (candidate.size()!=expected || crc32(candidate)!=get32(encoded,9)) return false;
        record.swap(candidate);
        return true;
    }

private:
    std::string base_;
    std::vector<std::string> parts_,values_;
    std::vector<uint16_t> index_[256];
    bool valid_=false;
    // Flat JSON lexemes are retained byte-for-byte. No numeric parsing or
    // rounding. Unsupported structures use the general BD1 codec instead.
    static bool split(const std::string &s,std::vector<std::string> &parts,std::vector<std::string> &values) {
        parts.clear();values.clear();
        if(s.size()<2 || s.front()!='{' || s.back()!='}') return false;
        size_t i=1,previous=0;
        auto space=[&]() {while(i<s.size() && (s[i]==' ' || s[i]=='\t' || s[i]=='\r' || s[i]=='\n'))++i;};
        auto quoted=[&]() {
            if(i>=s.size() || s[i++]!='"')return false;
            while(i<s.size()) {char c=s[i++];if(c=='"')return true;if(c=='\\'){if(i==s.size())return false;++i;}}
            return false;
        };
        while(i<s.size()-1) {
            space();if(!quoted())return false;space();if(i>=s.size() || s[i++]!=':')return false;space();
            size_t start=i;
            if(i>=s.size())return false;
            if(s[i]=='"') {if(!quoted())return false;}
            else {
                while(i<s.size() && s[i]!=',' && s[i]!='}' && s[i]!=' ' && s[i]!='\t' && s[i]!='\r' && s[i]!='\n') {
                    if(s[i]=='[' || s[i]=='{' || s[i]=='"')return false;
                    ++i;
                }
            }
            if(i==start || values.size()>=128)return false;
            parts.push_back(s.substr(previous,start-previous));values.push_back(s.substr(start,i-start));previous=i;
            space();if(i==s.size()-1 && s[i]=='}') {parts.push_back(s.substr(previous));return true;}
            if(i>=s.size() || s[i++]!=',')return false;
        }
        parts.clear();values.clear();return false;
    }
    static void varint(std::string &out,size_t n) {
        do {unsigned byte=n&127;n>>=7;out.push_back(static_cast<char>(byte|(n?128:0)));}while(n);
    }
    static bool readvar(const std::string &s,size_t &i,size_t &n) {
        n=0;
        for(unsigned shift=0;shift<=14;shift+=7) {
            if(i==s.size())return false;
            unsigned byte=static_cast<unsigned char>(s[i++]);n|=(byte&127)<<shift;
            if(!(byte&128))return n<=MaxRecord;
        }
        return false;
    }
    static std::string scalar(const std::string &s,bool patch,size_t prefix=0,size_t suffix=0) {
        bool numeric=!s.empty();for(char c:s)if(nibble(c)<0)numeric=false;
        std::string out(1,static_cast<char>((patch?2:0)+(numeric?1:0)));
        if(patch){varint(out,prefix);varint(out,suffix);}
        varint(out,s.size());
        if(numeric) {
            for(size_t j=0;j<s.size();j+=2) out.push_back(static_cast<char>((nibble(s[j])<<4)|(j+1<s.size()?nibble(s[j+1]):0)));
        } else out+=s;
        return out;
    }
    bool encodeFields(const std::string &record,std::string &out)const {
        std::vector<std::string> parts,values;
        if(!split(record,parts,values) || parts!=parts_ || values.size()!=values_.size())return false;
        out="BS1";put16(out,static_cast<uint16_t>(record.size()));put32(out,crc32(base_));put32(out,crc32(record));
        size_t bitmap=out.size();out.append((values.size()+7)/8,'\0');
        for(size_t k=0;k<values.size();++k) {
            const auto &s=values[k];const auto &old=values_[k];if(s==old)continue;
            out[bitmap+k/8]=static_cast<char>(static_cast<unsigned char>(out[bitmap+k/8])|(1U<<(k%8)));
            std::string plain=scalar(s,false);size_t prefix=0,suffix=0;
            while(prefix<s.size() && prefix<old.size() && s[prefix]==old[prefix])++prefix;
            while(suffix<s.size()-prefix && suffix<old.size()-prefix && s[s.size()-1-suffix]==old[old.size()-1-suffix])++suffix;
            std::string changed=scalar(s.substr(prefix,s.size()-prefix-suffix),true,prefix,suffix);
            out+=(changed.size()<plain.size()?changed:plain);
        }
        return true;
    }
    bool decodeFields(const std::string &s,std::string &out)const {
        size_t expected=get16(s,3),bitmap=(values_.size()+7)/8,i=13+bitmap;
        if(values_.empty() || parts_.size()!=values_.size()+1 || expected>MaxRecord || s.size()<i || get32(s,5)!=crc32(base_))return false;
        std::string candidate;
        for(size_t k=0;k<values_.size();++k) {
            std::string value=values_[k];
            if(static_cast<unsigned char>(s[13+k/8])&(1U<<(k%8))) {
                if(i==s.size())return false;
                unsigned mode=static_cast<unsigned char>(s[i++]);if(mode>3)return false;
                size_t prefix=0,suffix=0,length=0;
                if(mode>=2 && (!readvar(s,i,prefix)||!readvar(s,i,suffix)))return false;
                if(!readvar(s,i,length) || prefix>value.size() || suffix>value.size()-prefix)return false;
                size_t bytes=(mode&1)?(length+1)/2:length;
                if(bytes>s.size()-i)return false;
                std::string middle;
                if(mode&1)for(size_t j=0;j<length;++j){unsigned byte=static_cast<unsigned char>(s[i+j/2]);middle.push_back(alphabet()[j%2?byte&15:byte>>4]);}
                else middle=s.substr(i,length);
                i+=bytes;
                value=value.substr(0,prefix)+middle+value.substr(value.size()-suffix);
            }
            if(candidate.size()+parts_[k].size()+value.size()>expected)return false;
            candidate+=parts_[k];candidate+=value;
        }
        candidate+=parts_.back();
        if(i!=s.size() || candidate.size()!=expected || crc32(candidate)!=get32(s,9))return false;
        out.swap(candidate);return true;
    }
    static const char *alphabet() { return "0123456789.-:+eTZ"; }
    static int nibble(char c) {
        for (int i=0;i<16;++i) if (alphabet()[i]==c) return i;
        return -1;
    }
    static unsigned hash(const std::string &s,size_t i) {
        return (static_cast<unsigned char>(s[i])*31U+static_cast<unsigned char>(s[i+1])*17U+static_cast<unsigned char>(s[i+2]))&255U;
    }
    void match(const std::string &s,size_t i,size_t &length,size_t &offset) const {
        if (s.size()-i<4) return;
        const auto &bucket=index_[hash(s,i)];
        // Fixed candidate count and match length bound CPU cost on a busy node.
        size_t checked=0;
        for (auto it=bucket.rbegin();it!=bucket.rend() && checked<64;++it,++checked) {
            size_t pos=*it,n=0,limit=std::min<size_t>(129,std::min(s.size()-i,base_.size()-pos));
            while(n<limit && s[i+n]==base_[pos+n]) ++n;
            if(n>length) {length=n;offset=pos;}
        }
    }
    static void flush(std::string &literal,std::string &out) {
        if(literal.empty()) return;
        out.push_back(static_cast<char>(literal.size()-1)); out+=literal; literal.clear();
    }
    static void put16(std::string &s,uint16_t n) {
        s.push_back(static_cast<char>(n&255)); s.push_back(static_cast<char>(n>>8));
    }
    static void put32(std::string &s,uint32_t n) {
        for(int i=0;i<4;++i) s.push_back(static_cast<char>((n>>(8*i))&255));
    }
    static uint16_t get16(const std::string &s,size_t i) {
        return static_cast<unsigned char>(s[i]) | (static_cast<uint16_t>(static_cast<unsigned char>(s[i+1]))<<8);
    }
    static uint32_t get32(const std::string &s,size_t i) {
        uint32_t n=0;
        for(int k=0;k<4;++k) n|=static_cast<uint32_t>(static_cast<unsigned char>(s[i+k]))<<(8*k);
        return n;
    }
};
}
