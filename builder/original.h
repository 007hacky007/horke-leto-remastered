// The Original pack: every exported frame upscaled 3x with Real-ESRGAN AnimeVideo v3, then atlased.
#pragma once
#include "image.h"

namespace hb {
struct Upscaler {
    fs::path runner, models;   // realesrgan-ncnn-vulkan executable and its model folder
    std::string runner_sha256, model_name = "realesr-animevideov3";
    json model;                // pins["model"]
};

struct OriginalPaths { fs::path source, cache, out, evidence; Emit log = [](const std::string&) {}; };  // log: "[detail] ..." lines

constexpr int SCALE = 3, PAD = 16;
std::pair<std::vector<std::array<int, 2>>, std::vector<std::array<int, 5>>> atlas_layout(const std::vector<std::array<int, 2>>& sizes);
Image extruded(const Image& image);
Image extend_rgb(const Image& rgb, const std::vector<uint8_t>& mask);  // RGB under hidden pixels = nearest visible
// Runs the upscaler on a folder of PNGs (out gets the same names).
void run_upscaler(const Upscaler& upscaler, const fs::path& in, const fs::path& out, const fs::path& log, const char* jobs = "2:2:2");

// progress(done, total, resource name) after each resource.
void build_original(const OriginalPaths& paths, const Upscaler& upscaler, const json& resources,
                    const std::function<void(int, int, const std::string&)>& progress);
// Live-palette variants (pins "palette_variants") into paths.out/variants; needs the finished Original pack.
void build_palette_variants(const OriginalPaths& paths, const Upscaler& upscaler, const json& resources, const json& specs,
                            const std::function<void(int, int, const std::string&)>& progress);
void update_variant_catalog(const fs::path& pack, const std::string& identity, const std::string& name);
}
