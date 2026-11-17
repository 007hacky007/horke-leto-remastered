#include <doctest/doctest.h>
#include "script/scheduler.h"
#include "fixtures/script/object_fixture.h"
using namespace hl;
namespace {
struct Services:EngineServices {
 std::vector<std::string> visits;
 Result<StepOutcome> invoke(Process& p,const Instruction&) override {visits.push_back(p.object->name);p.state=ProcessState::waiting;return StepOutcome::yielded;}
};
ScriptObject program(std::initializer_list<unsigned> body,const char* name="scheduled") {return load_object(script_fixture::object(body),name).value();}
}
TEST_SUITE("script.scheduler") {
TEST_CASE("slot order and scene ownership follow DOS scheduler") {
 Interpreter vm;Services services;Scheduler scheduler(vm,services);auto a=program({64,7},"first"),b=program({64,7},"other-scene"),c=program({64,7},"last");
 REQUIRE(scheduler.spawn(a,0,1).has_value());REQUIRE(scheduler.spawn(b,0,2).has_value());REQUIRE(scheduler.spawn(c,0,1).has_value());scheduler.set_scene(1);
 REQUIRE(scheduler.advance(0).has_value());CHECK(services.visits==std::vector<std::string>{"first","last"});
 REQUIRE(scheduler.advance(1).has_value());CHECK(services.visits.size()==2);
 scheduler.set_scene(2);REQUIRE(scheduler.advance(2).has_value());CHECK(services.visits.back()=="other-scene");
}
TEST_CASE("wait decrements on an update and resumes on the following update") {
 Interpreter vm;Services services;Scheduler scheduler(vm,services);auto o=program({13,1,122,13,42,7});auto slot=scheduler.spawn(o,0,0);REQUIRE(slot.has_value());
 REQUIRE(scheduler.advance(0).has_value());CHECK(scheduler.process(slot.value()).wait_updates==1);
 REQUIRE(scheduler.advance(1).has_value());CHECK(scheduler.process(slot.value()).wait_updates==0);CHECK(scheduler.process(slot.value()).stack.empty());
 REQUIRE(scheduler.advance(2).has_value());CHECK(scheduler.process(slot.value()).stack==std::vector<int32_t>{42});CHECK(scheduler.process(slot.value()).state==ProcessState::terminated);
}
TEST_CASE("events resume suspended processes without fabricated completion") {
 Interpreter vm;Services services;Scheduler scheduler(vm,services);auto o=program({64,13,9,7});auto slot=scheduler.spawn(o,0,0).value();
 REQUIRE(scheduler.advance(0).has_value());REQUIRE(scheduler.advance(1).has_value());CHECK(scheduler.process(slot).stack.empty());
 REQUIRE(scheduler.wake(slot).has_value());REQUIRE(scheduler.advance(2).has_value());CHECK(scheduler.process(slot).stack==std::vector<int32_t>{9});
}
TEST_CASE("100 slots and instruction budgets fail explicitly") {
 Interpreter vm;Services services;Scheduler scheduler(vm,services);auto o=program({4,253,255,7});
 for(int i=0;i<100;++i)REQUIRE(scheduler.spawn(o,0,0).has_value());REQUIRE_FALSE(scheduler.spawn(o,0,0).has_value());
 auto r=scheduler.advance(0,8);REQUIRE_FALSE(r.has_value());CHECK(r.error().reason.find("budget")!=std::string::npos);CHECK(scheduler.process(0).state==ProcessState::ready);
}
}
