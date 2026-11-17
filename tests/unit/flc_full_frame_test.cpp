#include <doctest/doctest.h>
#include "data/flc.h"
#include "../fixtures/flc.h"
TEST_SUITE("data.flc.full") {
TEST_CASE("flc_palette_6bit_and_8bit"){
 for(auto [kind,value,want]:{std::array<unsigned,3>{11,63,252},std::array<unsigned,3>{4,200,200}}){auto b=fixture::flc({fixture::frame({fixture::chunk(kind,fixture::raw({1,0,2,1,value,0,1})),fixture::chunk(13,{})})});auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());auto f=d.value().next();REQUIRE(f.has_value());REQUIRE(f.value());CHECK(f.value()->palette[2][0]==want);CHECK(f.value()->palette[2][2]==(kind==11?4:1));}
}
TEST_CASE("flc_copy_black_and_run_chunks"){
 for(auto [kind,payload,want]:{std::tuple{16,fixture::raw({1,2,3,4}),fixture::raw({1,2,3,4})},std::tuple{13,fixture::raw({}),fixture::raw({0,0,0,0})},std::tuple{15,fixture::raw({1,2,7,1,254,3,4}),fixture::raw({7,7,3,4})}}){auto d=hl::FlcDecoder::open(fixture::flc({fixture::frame({fixture::chunk(kind,payload)})}));REQUIRE(d.has_value());auto f=d.value().next();REQUIRE(f.has_value());REQUIRE(f.value());std::vector<std::byte> actual;for(auto v:f.value()->indices)actual.push_back(std::byte(v));CHECK(actual==want);}
}
TEST_CASE("flc_invalid_packet_length"){
 for(auto payload:{fixture::raw({1,3,7,1,2,3}),fixture::raw({1,0,7}),fixture::raw({1,254,1})}){auto d=hl::FlcDecoder::open(fixture::flc({fixture::frame({fixture::chunk(15,payload)})}));REQUIRE(d.has_value());CHECK_FALSE(d.value().next().has_value());}
}
}
