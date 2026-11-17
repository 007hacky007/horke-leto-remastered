#pragma once
#include "data/result.h"
#include <filesystem>
#include <vector>
namespace hl {
struct PngImage {uint32_t width{},height{},channels{};std::vector<uint8_t> pixels;};
Result<void> write_png(const std::filesystem::path&,uint32_t width,uint32_t height,uint32_t channels,std::span<const uint8_t> pixels);
Result<PngImage> read_png(const std::filesystem::path&,uint32_t channels);
}
