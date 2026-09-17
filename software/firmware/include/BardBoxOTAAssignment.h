#pragma once
#include "BardBoxOTAManifest.h"

namespace bardbox {
struct OTAAssignment {
    uint32_t generation=0;
    OTAManifest manifest;
    std::string signature, artifactPath;
};

// Deliberately narrow parser for the ASCII server response. Rejects duplicate or
// unknown fields, escapes, coercions and excessive input. No recursive JSON tree.
// Escapes are unnecessary for all values emitted by this protocol's server.
class OTAAssignmentParser {
public:
    static bool parse(const std::string &json, OTAAssignment &assignment) {
        if(json.size()>4096) return false;
        OTAAssignmentParser parser(json);
        OTAAssignment next;
        const char *fields[]={"generation","envelope","artifact_path"};
        if(!parser.object(fields,3,[&](size_t field) {
            if(field==0) return parser.number(next.generation);
            if(field==1) return parser.envelope(next);
            return parser.string(next.artifactPath,128);
        })) return false;
        parser.space();
        std::string canonical;
        if(parser.pos_!=json.size() || !next.generation || !canonicalOTA(next.manifest,canonical)) return false;
        if(next.manifest.releaseId=="." || next.manifest.releaseId==".." ||
           next.artifactPath!="/ota/v1/device/artifact/"+next.manifest.releaseId) return false;
        if(next.signature.empty() || next.signature.size()%4) return false;
        size_t padding=0;
        for(char c:next.signature) {
            if(c=='=') { if(++padding>2) return false; }
            else if(padding || !((c>='A' && c<='Z') || (c>='a' && c<='z') ||
                (c>='0' && c<='9') || c=='+' || c=='/')) return false;
        }
        assignment=next;
        return true;
    }
private:
    explicit OTAAssignmentParser(const std::string &json) : json_(json) {}
    void space() {
        while(pos_<json_.size() && (json_[pos_]==' ' || json_[pos_]=='\t' ||
              json_[pos_]=='\n' || json_[pos_]=='\r')) ++pos_;
    }
    bool take(char c) {
        space();
        if(pos_==json_.size() || json_[pos_]!=c) return false;
        ++pos_; return true;
    }
    bool string(std::string &out,size_t limit) {
        if(!take('"')) return false;
        out.clear();
        while(pos_<json_.size()) {
            unsigned char c=json_[pos_++];
            if(c=='"') return true;
            if(c<32 || c>126 || c=='\\' || out.size()==limit) return false;
            out.push_back(static_cast<char>(c));
        }
        return false;
    }
    bool number(uint32_t &value) {
        space();
        if(pos_==json_.size() || json_[pos_]<'0' || json_[pos_]>'9') return false;
        bool zero=json_[pos_]=='0';
        size_t start=pos_;
        value=0;
        while(pos_<json_.size() && json_[pos_]>='0' && json_[pos_]<='9') {
            unsigned digit=static_cast<unsigned>(json_[pos_++]-'0');
            if(value>(UINT32_MAX-digit)/10) return false;
            value=value*10+digit;
        }
        return !zero || pos_==start+1;
    }
    template<class Parse>
    bool object(const char *const *names,size_t count,Parse parseField) {
        if(!take('{')) return false;
        uint32_t seen=0;
        for(size_t field=0;field<count;++field) {
            if(field && !take(',')) return false;
            std::string key;
            if(!string(key,32) || !take(':')) return false;
            size_t index=0;
            while(index<count && key!=names[index]) ++index;
            if(index==count || (seen&(1U<<index))) return false;
            seen|=1U<<index;
            if(!parseField(index)) return false;
        }
        return take('}') && seen==((1U<<count)-1);
    }
    bool envelope(OTAAssignment &a) {
        const char *fields[]={"manifest","signature"};
        return object(fields,2,[&](size_t field) {
            return field==0 ? manifest(a.manifest) : string(a.signature,104);
        });
    }
    bool manifest(OTAManifest &m) {
        const char *fields[]={"format","release_id","component","version","target","layout",
            "config_schema","queue_schema","size","sha256","key_id"};
        std::string *values[]={&m.format,&m.releaseId,&m.component,&m.version,&m.target,&m.layout,
            &m.configSchema,&m.queueSchema,nullptr,&m.sha256,&m.keyId};
        return object(fields,11,[&](size_t field) {
            return field==8 ? number(m.size) : string(*values[field],field==9 ? 64 : 96);
        });
    }
    const std::string &json_;
    size_t pos_=0;
};
} // namespace bardbox
