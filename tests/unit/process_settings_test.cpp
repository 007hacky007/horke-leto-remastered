#include <doctest/doctest.h>
#include "script/interpreter.h"
#include "fixtures/script/object_fixture.h"
using namespace hl;
namespace {struct Reject:EngineServices {Result<StepOutcome> invoke(Process&,const Instruction&)override{return DataError{"","",0,"unbound"};}};}
TEST_SUITE("script.settings") {
TEST_CASE("DOS process reset and animation parameters are local to the process") {
 auto object=load_object(script_fixture::object({123,13,7,124,13,3,125,13,9,128,13,11,13,12,129,13,4,132,13,8,165,13,25,167,114,7}),"settings").value();
 Interpreter vm;Reject services;Process p{object};auto r=vm.run(p,services,100);REQUIRE(r.has_value());
 CHECK(p.animation.z==7);CHECK(p.animation.cycles==3);CHECK(p.animation.volume==9);CHECK(p.animation.x==11);CHECK(p.animation.y==12);CHECK(p.animation.speed==4);CHECK(p.animation.delay==8);CHECK(p.animation.sample_priority==25);CHECK(p.animation.phase_controlled);CHECK(p.stack.empty());
 Process q{object};CHECK(q.animation.cycles==1);CHECK(q.animation.speed==100);CHECK(q.animation.delay==-1);
}
TEST_CASE("settings validate the complete stack before mutation") {
 auto object=load_object(script_fixture::object({129,7}),"settings").value();Interpreter vm;Reject services;Process p{object};p.pc=17;p.stack={12};
 auto r=vm.step(p,services);REQUIRE_FALSE(r.has_value());CHECK(p.stack==std::vector<int32_t>{12});CHECK(p.animation.x==0);CHECK(p.pc==17);
}
}
