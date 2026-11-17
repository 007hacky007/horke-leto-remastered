#include <doctest/doctest.h>
#include "data/flc.h"
#include "../fixtures/flc.h"
namespace { std::vector<uint8_t> pixels(hl::FlcDecoder& d){auto r=d.next();REQUIRE(r.has_value());REQUIRE(r.value());return r.value()->indices;} }
TEST_SUITE("data.flc.delta") {
TEST_CASE("flc_line_delta_skips_and_signed_runs"){
 auto b=fixture::flc({fixture::frame({fixture::chunk(16,fixture::raw({1,2,3,4}))}),fixture::frame({fixture::chunk(12,fixture::raw({1,0,1,0,1,1,255,9}))})},2);
 auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());CHECK(pixels(d.value())==std::vector<uint8_t>{1,2,3,4});CHECK(pixels(d.value())==std::vector<uint8_t>{1,2,3,9});
}
TEST_CASE("flc_word_delta_skips_and_odd_last_pixel"){
 auto b=fixture::flc({fixture::frame({fixture::chunk(16,fixture::raw({1,2,3,4}))}),fixture::frame({fixture::chunk(7,fixture::raw({1,0,255,255,9,128,0,0}))})},2);
 auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());pixels(d.value());CHECK(pixels(d.value())==std::vector<uint8_t>{1,2,3,9});
}
TEST_CASE("flc_word_delta_signed_pair_runs"){
 for(auto packet:{fixture::raw({1,0,1,0,0,1,8,9}),fixture::raw({1,0,1,0,0,255,8,9})}){auto b=fixture::flc({fixture::frame({fixture::chunk(7,packet)})});auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());CHECK(pixels(d.value())==std::vector<uint8_t>{8,9,0,0});}
}
TEST_CASE("flc_palette_persistence_and_replay"){
 auto b=fixture::flc({fixture::frame({fixture::chunk(4,fixture::raw({1,0,1,1,9,8,7})),fixture::chunk(16,fixture::raw({1,2,3,4}))}),fixture::frame({fixture::chunk(7,fixture::raw({0,0}))})},2);
 auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());pixels(d.value());auto f=d.value().next().value().value();CHECK(f.palette[1]==std::array<uint8_t,3>{9,8,7});REQUIRE(d.value().rewind().has_value());CHECK(pixels(d.value())==std::vector<uint8_t>{1,2,3,4});auto seek=d.value().seek(1);REQUIRE(seek.has_value());CHECK(seek.value().indices==f.indices);CHECK(seek.value().palette==f.palette);CHECK_FALSE(d.value().seek(2).has_value());
}
TEST_CASE("flc_delta_truncation_and_line_overflow"){
 for(auto data:{fixture::raw({1,0,1,0,0,1,9}),fixture::raw({1,0,254,255,0,0}),fixture::raw({1,0,1,0,2,1,1,2})}){auto d=hl::FlcDecoder::open(fixture::flc({fixture::frame({fixture::chunk(7,data)})}));REQUIRE(d.has_value());CHECK_FALSE(d.value().next().has_value());}
}
TEST_CASE("flc_fractional_delays_do_not_drift"){hl::FrameClock c;for(int i=0;i<3500;++i)c.advance({1,35});CHECK(c.elapsed_nanoseconds()==100000000000ULL);}
}
