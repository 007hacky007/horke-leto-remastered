#include <doctest/doctest.h>
#include "script/interpreter.h"
#include "fixtures/script/object_fixture.h"
using namespace hl;
namespace {
struct NoServices:EngineServices {Result<StepOutcome> invoke(Process&,const Instruction&) override {return DataError{"","service",0,"unbound service"};}};
ScriptObject program(std::initializer_list<unsigned> code) {return load_object(script_fixture::object(code),"test-object").value();}
}
TEST_SUITE("script.interpreter") {
TEST_CASE("signed immediates and wrapping arithmetic match x86") {
 NoServices s;Interpreter vm;auto o=program({9,255,255,255,127,13,1,23,7});Process p{o};
 for(int i=0;i<4;++i)REQUIRE(vm.step(p,s).has_value());CHECK(p.stack==std::vector<int32_t>{INT32_MIN});
 CHECK(vm.step(p,s).value()==StepOutcome::terminated);
}
TEST_CASE("division remainder and masked shifts preserve DOS signed semantics") {
 NoServices s;Interpreter vm;
 for(auto form:{std::pair{26,-2},std::pair{27,-1}}) {
  auto o=program({13,249,13,3,unsigned(form.first),7});Process p{o};for(int i=0;i<4;++i)REQUIRE(vm.step(p,s).has_value());CHECK(p.stack.back()==form.second);
 }
 auto o=program({13,255,13,33,33,7});Process p{o};for(int i=0;i<4;++i)REQUIRE(vm.step(p,s).has_value());CHECK(p.stack.back()==-1);
}
TEST_CASE("globals use resource names and preserve pre/post assignment results") {
 NoServices s;Interpreter vm;auto o=program({13,41,13,0,43,13,0,40,16,0,7});Process p{o};
 for(int i=0;i<7;++i)REQUIRE(vm.step(p,s).has_value());CHECK(vm.globals().at("x")==42);CHECK(p.stack==std::vector<int32_t>{41,42});
}
TEST_CASE("conditional branches consume their condition") {
 NoServices s;Interpreter vm;auto o=program({13,0,3,2,0,13,99,13,7,7});Process p{o};
 for(int i=0;i<4;++i)REQUIRE(vm.step(p,s).has_value());CHECK(p.stack==std::vector<int32_t>{7});
}
TEST_CASE("stack bounds zero division and unbound services stop with provenance") {
 NoServices s;Interpreter vm;
 for(auto body:{std::initializer_list<unsigned>{5,7},{13,1,13,0,26,7},{64,7}}) {
  auto o=program(body);Process p{o};Result<StepOutcome> r=StepOutcome::running;
  for(int i=0;i<8&&r.has_value();++i)r=vm.step(p,s);
  REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="test-object");CHECK(r.error().byte_offset>=17);
 }
 auto o=program({13,1,7});Process p{o};p.pc=17;p.stack.resize(100);auto r=vm.step(p,s);REQUIRE_FALSE(r.has_value());CHECK(r.error().reason.find("overflow")!=std::string::npos);
}
TEST_CASE("declarations skip handler bodies and budget exhaustion is an error") {
 NoServices s;Interpreter vm;auto o=program({55,3,0,13,99,7,13,1,7});Process p{o};
 REQUIRE(vm.step(p,s).has_value());REQUIRE(vm.step(p,s).has_value());CHECK(p.pc==23);CHECK(p.stack.empty());
 auto loop=program({4,253,255,7});Process q{loop};auto r=vm.run(q,s,20);REQUIRE_FALSE(r.has_value());CHECK(r.error().reason.find("budget")!=std::string::npos);CHECK(q.stack.empty());CHECK(q.state==ProcessState::ready);
}
TEST_CASE("global identity is case sensitive and the DOS limit is 1000") {
 NoServices services;Interpreter vm;auto o=program({13,1,13,0,43,7});
 o.layout.resources[0].name="X";Process p{o};REQUIRE(vm.run(p,services,20).has_value());CHECK(vm.globals().contains("X"));CHECK_FALSE(vm.globals().contains("x"));
 for(int i=1;i<1000;++i){o.layout.resources[0].name="variable"+std::to_string(i);Process q{o};REQUIRE(vm.run(q,services,20).has_value());}
 o.layout.resources[0].name="overflow";Process q{o};auto r=vm.run(q,services,20);REQUIRE_FALSE(r.has_value());CHECK(r.error().reason.find("global")!=std::string::npos);
}

TEST_CASE("unknown instructions cannot reach a permissive service") {
 struct Permissive:EngineServices {bool called{};Result<StepOutcome> invoke(Process&,const Instruction&)override{called=true;return StepOutcome::running;}} service;
 auto o=program({13,1,7});o.instructions[1].opcode=255;Process p{o};p.pc=17;Interpreter vm;
 auto r=vm.step(p,service);REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="test-object");CHECK(r.error().byte_offset==17);CHECK_FALSE(service.called);
}

}
TEST_SUITE("script.interpreter") {
TEST_CASE("nested DOS service failures preserve the originating object and offset") {
 struct Nested:EngineServices {Result<StepOutcome> invoke(Process&,const Instruction&)override{return DataError{"","child-object",99,"child service failed",true};}} service;
 auto o=load_object(script_fixture::object({71,7}),"parent").value();Process p{o};p.pc=17;Interpreter vm;auto r=vm.step(p,service);REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="child-object");CHECK(r.error().byte_offset==99);
}

}
TEST_SUITE("script.interpreter") {
TEST_CASE("DOS arithmetic and comparison families retain their recorded integer results") {
 const int values[]={0,1,1,0,1,0,10,4,21,2,1,3,7,4,-4,56,0,1,1,0,-3};
 for(unsigned op=17;op<=37;++op){auto o=load_object(script_fixture::object({13,7,13,3,op,7}),"integer-family").value();Process p{o};Interpreter vm;NoServices services;auto r=vm.run(p,services,100);REQUIRE(r.has_value());CHECK(p.stack.back()==values[op-17]);}
}
}
TEST_SUITE("script.interpreter") {
TEST_CASE("DOS random uses the verified CRT sequence and rejects zero modulus") {
 uint32_t state=1;for(int value:{38,58,13,15,51}){auto r=dos_random(state,100);REQUIRE(r.has_value());CHECK(r.value()==value);}
 auto before=state;CHECK_FALSE(dos_random(state,0).has_value());CHECK(state==before);
}
}
