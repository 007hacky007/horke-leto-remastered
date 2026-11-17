#pragma once
#include "le/le_image.h"
namespace hl {
struct LeFixup { std::uint32_t page, source_object, source_offset, target_object, target_offset, value; unsigned flags; };
struct RelocatedImage { std::vector<LeObject> objects; std::uint32_t entry; std::vector<LeFixup> fixups; };
Result<RelocatedImage> apply_fixups(const LeImage& image);
}
