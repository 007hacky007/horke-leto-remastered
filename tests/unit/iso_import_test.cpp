#include <doctest/doctest.h>
#include "data/disc.h"
TEST_SUITE("data.iso") {
TEST_CASE("iso_extent_bounds") {CHECK_FALSE(hl::validate_iso_extent({9,2049},10).has_value());CHECK_FALSE(hl::validate_iso_extent({UINT32_MAX,2048},10).has_value());CHECK(hl::validate_iso_extent({9,2048},10).has_value());}
TEST_CASE("iso_directory_record_bounds") {std::vector<std::byte> b(40);b[0]=std::byte{34};CHECK_FALSE(hl::parse_iso_directory(b,100).has_value());b[0]=std::byte{255};CHECK_FALSE(hl::parse_iso_directory(b,100).has_value());}
}
