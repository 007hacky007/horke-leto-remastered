#include <doctest/doctest.h>
#include "audio/sample_schedule.h"
using namespace hl;
TEST_SUITE("audio.schedule") {
TEST_CASE("DOS start delay adds initial cycle and gap waits only while idle") {
 uint32_t rng=1;auto r=SampleSchedule::begin(2,3,4,0,rng);REQUIRE(r.has_value());auto s=r.value();CHECK_FALSE(s.start_now);for(int i=0;i<3;++i)CHECK_FALSE(s.advance(false,rng));CHECK(s.advance(false,rng));CHECK_FALSE(s.advance(true,rng));for(int i=0;i<4;++i)CHECK_FALSE(s.advance(false,rng));CHECK(s.advance(false,rng));for(int i=0;i<5;++i)CHECK_FALSE(s.advance(false,rng));CHECK(s.done);
}
TEST_CASE("ordinary voice starts now and completes without a silent repeat") {uint32_t rng=1;auto s=SampleSchedule::begin(1,-1,0,0,rng).value();CHECK(s.start_now);CHECK_FALSE(s.advance(true,rng));CHECK_FALSE(s.advance(false,rng));CHECK(s.done);}
TEST_CASE("random gap bounds are validated and deterministic") {uint32_t rng=1;CHECK_FALSE(SampleSchedule::begin(1,0,200,100,rng).has_value());auto a=SampleSchedule::begin(2,0,10,20,rng);REQUIRE(a.has_value());CHECK(a.value().countdown>=10);CHECK(a.value().countdown<20);}
}
