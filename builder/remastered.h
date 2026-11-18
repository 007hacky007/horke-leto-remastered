#pragma once
#include "original.h"

namespace hb {
// source: exported frames; art: decrypted art (manifest.json + images); original: the Original pack.
struct RemasteredPaths { fs::path source, art, original, output, cache; Emit log = [](const std::string&) {}; };
void build_remastered(const RemasteredPaths& paths, const Upscaler& upscaler,
                      const std::function<void(int, int, const std::string&)>& progress);
// Graded copies of Original palette variants for animations; static redraws are copied when the palette change is invisible.
void build_remastered_variants(const RemasteredPaths& paths, const json& specs);
}
