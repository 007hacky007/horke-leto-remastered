#include <doctest/doctest.h>
#include "le/le_image.h"
#include "../fixtures/le/synthetic.h"
using namespace hl;
TEST_SUITE("le.image") {
TEST_CASE("le_object_pages_map") {
 auto b=fixture::le();auto r=parse_le(b);REQUIRE(r.has_value());auto& x=r.value();
 REQUIRE(x.objects.size()==2);CHECK(x.objects[0].bytes[0]==std::byte{0xa0});CHECK(x.objects[0].bytes[16]==std::byte{0xc0});
 CHECK(x.objects[1].bytes[0]==std::byte{0});CHECK(x.objects[0].base==0x10000);
}
TEST_CASE("le_zero_fill_and_entry") {auto r=parse_le(fixture::le());REQUIRE(r.has_value());CHECK(r.value().entry==0x10007);CHECK(r.value().objects[0].bytes[39]==std::byte{0});}
TEST_CASE("le_truncated_table_rejected") {auto b=fixture::le();b.resize(0x160);CHECK_FALSE(parse_le(b).has_value());}
TEST_CASE("le_offset_overflow_rejected") {auto b=fixture::le();fixture::put(b,0x80+0x40,0xfffffff0);CHECK_FALSE(parse_le(b).has_value());}
TEST_CASE("le_unsupported_page_rejected") {auto b=fixture::le();b[0x177]=std::byte{1};CHECK_FALSE(parse_le(b).has_value());}
}
