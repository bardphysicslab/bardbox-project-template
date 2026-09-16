#include "BardBoxOTAAssignment.h"
#include <cassert>
#include <iostream>
#include <iterator>
int main() {
    std::string json((std::istreambuf_iterator<char>(std::cin)),std::istreambuf_iterator<char>());
    bardbox::OTAAssignment result;
    result.generation=123; result.signature="unchanged";
    if(!bardbox::OTAAssignmentParser::parse(json,result)) {
        assert(result.generation==123 && result.signature=="unchanged");
        return 1;
    }
    std::string bytes;
    assert(bardbox::canonicalOTA(result.manifest,bytes));
    std::cout<<result.generation<<'\n'<<result.signature<<'\n'<<result.artifactPath<<'\n'<<bytes;
}
