#pragma once
#include "data/result.h"
#include <cstdint>
#include <vector>
namespace hl {
struct LeObject {
    std::uint32_t base, flags, first_page, page_count;
    std::vector<std::byte> bytes;
};
struct LeImage {
    std::vector<LeObject> objects;
    std::uint32_t entry{}, page_size{}, page_count{};
    std::vector<std::uint32_t> fixup_pages;
    std::vector<std::byte> fixup_records;
};
Result<LeImage> parse_le(ByteView input);
}
