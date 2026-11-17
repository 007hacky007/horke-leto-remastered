#pragma once
#include "le/le_fixups.h"
#include <filesystem>
namespace hl {
Result<std::vector<std::byte>> make_elf(const RelocatedImage& image);
Result<void> write_elf(const RelocatedImage& image,const std::filesystem::path& path);
}
