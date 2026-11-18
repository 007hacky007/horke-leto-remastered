#pragma once
#include "original.h"

namespace hb {
// source: exported frames; art: decrypted art (manifest.json + images); original: the Original pack.
struct RemasteredPaths { fs::path source, art, original, output, cache; Emit log = [](const std::string&) {}; };
void build_remastered(const RemasteredPaths& paths, const Upscaler& upscaler,
                      const std::function<void(int, int, const std::string&)>& progress);
// Graded copies of the Original palette variants for remastered animations (static redraws fall back to Original).
void build_remastered_variants(const RemasteredPaths& paths, const json& specs);
}
