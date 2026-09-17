#include "BardBoxRecordDelta.h"
#include "BardBoxRecordChunk.h"
#include <cassert>
#include <iostream>
#include <random>

int main(int argc,char **argv) {
    if(argc>1 && std::string(argv[1])=="stream") {
        std::string base,line,out,decoded;
        size_t count=0,total=0;
        while(std::getline(std::cin,line)) {
            if(count%50==0) {base=line;total+=bardbox::RecordChunk::header(1).size();}
            bardbox::RecordDelta codec(base);
            assert(codec.encode(line,out)); assert(codec.decode(out,decoded)); assert(decoded==line);
            std::string framed;
            assert(bardbox::RecordChunk::frame(count%50 ? out : base,framed));
            total+=framed.size(); ++count;
        }
        std::cout<<count<<" "<<total<<"\n";
        return 0;
    }
    std::mt19937 rng(17);
    for(size_t size: {0,1,128,256,2300,8192}) {
        std::string base(size,'x'),encoded,decoded;
        for(char &c:base) c=static_cast<char>(rng());
        bardbox::RecordDelta codec(base);
        for(int iteration=0;iteration<10;++iteration) {
            std::string record=base;
            for(char &c:record) if(rng()%8==0) c=static_cast<char>(rng());
            assert(codec.encode(record,encoded)); assert(codec.decode(encoded,decoded)); assert(record==decoded);
            for(size_t n=0;n<encoded.size();++n) {
                assert(!codec.decode(encoded.substr(0,n),decoded));
            }
            if(!encoded.empty()) {
                encoded[encoded.size()-1]^=1;
                // A padding nibble may be unused, but never accept changed output.
                bool ok=codec.decode(encoded,decoded); assert(!ok || decoded==record);
            }
        }
    }
    bardbox::RecordDelta codec("{\"value\":123.45,\"timestamp\":\"2026-09-15T12:00:00Z\"}");
    std::string encoded,out;
    assert(codec.encode("{\"value\":-543.21,\"timestamp\":\"2026-09-29T12:02:00Z\"}",encoded));
    assert(codec.decode(encoded,out));
    const std::string expected=out;
    assert(encoded.substr(0,3)=="BS1");
    for(size_t i=0;i<encoded.size();++i) {
        assert(!codec.decode(encoded.substr(0,i),out));
        for(unsigned bit=0;bit<8;++bit) {
            std::string corrupt=encoded;corrupt[i]^=static_cast<char>(1U<<bit);
            bool ok=codec.decode(corrupt,out);assert(!ok || out==expected);
        }
    }
    const std::string records[]={"{\"new_key\":[1,2,3]}",
        "{\"value\":null,\"timestamp\":\"quoted \\\" text\"}",
        "{\"value\":true,\"timestamp\":\"\\u0000\"}",
        "{\"value\":-0.000001e+23,\"timestamp\":\"\"}"};
    for(const auto &record:records){assert(codec.encode(record,encoded));assert(codec.decode(encoded,out));assert(out==record);}
    assert(!bardbox::RecordDelta("wrong dictionary").decode(encoded,out));
    assert(!codec.encode(std::string(8193,'a'),out));
    for(int i=0;i<1000;++i) {
        std::string garbage(rng()%100,'a');
        for(char &c:garbage)c=static_cast<char>(rng());
        codec.decode(garbage,out);
    }
    std::cout<<"record delta tests passed\n";
}
