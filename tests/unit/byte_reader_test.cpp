#include <doctest/doctest.h>
#include "data/byte_reader.h"
#include <array>
#include <limits>
TEST_SUITE("data.bytes") {
TEST_CASE("byte_reads_stop_at_end") {
 std::array b{std::byte{0x34},std::byte{0x12},std::byte{0xab}};
 hl::ByteReader r(b,"fixture","bytes");
 REQUIRE(r.u16le().value()==0x1234); CHECK_FALSE(r.u16le().has_value());
 CHECK(r.position()==2); CHECK(r.take(1).value()[0]==std::byte{0xab});
 CHECK_FALSE(r.take(1).has_value());
}
TEST_CASE("offset_addition_cannot_wrap") {
 hl::ByteReader r({}); CHECK_FALSE(r.take(std::numeric_limits<size_t>::max()).has_value());
 CHECK_FALSE(hl::checked_range(10,std::numeric_limits<size_t>::max(),2));
 CHECK(hl::checked_range(10,10,0));
}
}
