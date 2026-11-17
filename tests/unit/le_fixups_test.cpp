#include <doctest/doctest.h>
#include "le/le_fixups.h"
#include "../fixtures/le/synthetic.h"
using namespace hl;
TEST_SUITE("le.fixups") {
TEST_CASE("internal_offset32_short_and_wide_target") {
 for(bool wide:{false,true}) {auto m=parse_le(fixture::fixup(wide));REQUIRE(m.has_value());auto r=apply_fixups(m.value());REQUIRE(r.has_value());
 CHECK(r.value().fixups.size()==1);auto& b=r.value().objects[0].bytes;CHECK(b[4]==std::byte{8});CHECK(b[5]==std::byte{0});CHECK(b[6]==std::byte{2});CHECK(b[7]==std::byte{0});}
}
TEST_CASE("page_boundary_source") {auto m=parse_le(fixture::fixup(false,14));REQUIRE(m.has_value());auto r=apply_fixups(m.value());REQUIRE(r.has_value());CHECK(r.value().objects[0].bytes[16]==std::byte{2});}
TEST_CASE("invalid_object_and_target_rejected") {
 for(auto b:{fixture::fixup(false,4,0),fixture::fixup(false,4,3),fixture::fixup(true,4,2,0xffffffff)}) {auto m=parse_le(b);REQUIRE(m.has_value());CHECK_FALSE(apply_fixups(m.value()).has_value());}
}
TEST_CASE("unsupported_list_chain_addend_external_rejected") {
 for(auto [type,flags]:{std::pair{0x27,0}, {0x17,0}, {7,4}, {7,1}, {7,0x40}}) {auto b=fixture::fixup();fixture::put(b,0x1a0,type,1);fixture::put(b,0x1a1,flags,1);auto m=parse_le(b);REQUIRE(m.has_value());CHECK_FALSE(apply_fixups(m.value()).has_value());}
}
TEST_CASE("truncated_record_rejected") {auto b=fixture::fixup();fixture::put(b,0x184,6);auto m=parse_le(b);REQUIRE(m.has_value());CHECK_FALSE(apply_fixups(m.value()).has_value());}
}
