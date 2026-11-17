#pragma once
#include "data/text.h"
#include <array>
namespace hl {
struct FontSource {ByteView bytes;size_t offset{};};
struct BitmapGlyph {char32_t codepoint{};std::array<uint8_t,16> rows{};};
struct BitmapFont {std::array<BitmapGlyph,256> glyphs;uint32_t width{8},height{16},advance{8},baseline{12};};
Result<BitmapFont> load_original_font(const FontSource&);
Result<std::vector<uint8_t>> render_font(const BitmapFont&,std::span<const uint8_t> text);
}
