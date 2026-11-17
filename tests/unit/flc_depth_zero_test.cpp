#include <doctest/doctest.h>
#include "data/flc.h"
#include "../fixtures/flc.h"
namespace {
fixture::Bytes exceptional(unsigned count,unsigned delay){
 fixture::Bytes run;for(unsigned y=0;y<480;++y){run.push_back(std::byte{6});for(int i=0;i<5;++i){run.push_back(std::byte{127});run.push_back(std::byte{0});}run.push_back(std::byte{5});run.push_back(std::byte{0});}
 std::vector<fixture::Bytes> frames{fixture::frame({fixture::chunk(18,fixture::raw({0,0,0,0,0,0})),fixture::chunk(4,fixture::raw({0,0})),fixture::chunk(15,run)})};
 for(unsigned i=1;i<=count;++i)frames.push_back(fixture::frame({fixture::chunk(7,fixture::raw({0,0}))}));
 auto b=fixture::flc(frames,count,0,delay);fixture::u16(b,8,640);fixture::u16(b,10,480);return b;
}
void check(std::string name,unsigned count,unsigned delay,unsigned ticks){auto d=hl::FlcDecoder::open(exceptional(count,delay),name);REQUIRE(d.has_value());uint64_t updates=0;for(unsigned i=0;i<count;++i){auto r=d.value().next();REQUIRE(r.has_value());REQUIRE(r.value());CHECK(r.value()->indices.size()==640*480);CHECK(r.value()->declared_delay.numerator==delay);CHECK(r.value()->effective_delay.numerator==ticks);CHECK(r.value()->effective_delay.denominator==1);CHECK(r.value()->effective_delay.unit==hl::DelayUnit::engine_updates);updates+=r.value()->effective_delay.numerator;}CHECK_FALSE(d.value().next().value());CHECK(updates==uint64_t(count)*ticks);}
}
TEST_SUITE("data.flc.depth_zero") {
TEST_CASE("IN-PR-MO.FLC"){check("IN-PR-MO.FLC",45,0,1);}
TEST_CASE("IN-PRE-M.FLC"){check("IN-PRE-M.FLC",200,0,1);}
TEST_CASE("MAPA7-BL.FLC"){check("MAPA7-BL.FLC",1,71,3);}
TEST_CASE("unknown_depth_zero_is_not_silently_patched"){CHECK_FALSE(hl::FlcDecoder::open(exceptional(45,0),"other.flc").has_value());}
}
