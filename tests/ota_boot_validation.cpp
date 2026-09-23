#include "BardBoxOTABootValidation.h"
#include <cassert>
using namespace bardbox;
int main(){
OTABootValidation v; using R=OTABootValidation::Result;
assert(v.observe(0,1,0,true,true)==R::Rollback);
v.begin(UINT32_MAX-100,5);
assert(v.observe(100,5,100,true,true)==R::Waiting);
assert(v.observe(100,6,100,true,true)==R::Confirm);
assert(v.observe(10000,6,100,true,true)==R::Waiting);
assert(v.observe(100,6,100,false,true)==R::Rollback);
assert(v.observe(100,6,100,true,false)==R::Rollback);
assert(v.observe(150000,6,150000,true,true)==R::Rollback);
v.begin(0,UINT32_MAX);assert(v.observe(120000,0,120000,true,true)==R::Confirm);
}
