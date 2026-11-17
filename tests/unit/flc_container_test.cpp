#include <doctest/doctest.h>
#include "data/flc.h"
#include "../fixtures/flc.h"
TEST_SUITE("data.flc.container") {
TEST_CASE("flc_container_bounds"){auto b=fixture::flc({fixture::frame({fixture::chunk(13,{})})});CHECK(hl::FlcDecoder::open(b).has_value());b.pop_back();CHECK_FALSE(hl::FlcDecoder::open(b).has_value());b=fixture::flc({fixture::frame({})});fixture::u32(b,128,UINT32_MAX);CHECK_FALSE(hl::FlcDecoder::open(b).has_value());}
TEST_CASE("flc_unknown_chunk_reports_offset"){auto b=fixture::flc({fixture::frame({fixture::chunk(999,{})})});auto d=hl::FlcDecoder::open(b,"unknown.flc");REQUIRE(d.has_value());auto r=d.value().next();REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="unknown.flc");CHECK(r.error().byte_offset==144);}
TEST_CASE("flc_ring_is_not_extra_visible_frame"){auto b=fixture::flc({fixture::frame({fixture::chunk(13,{})}),fixture::frame({fixture::chunk(16,fixture::raw({1,2,3,4}))})});auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());CHECK(d.value().ring_frames()==1);REQUIRE(d.value().next().value().has_value());CHECK_FALSE(d.value().next().value().has_value());}
}
