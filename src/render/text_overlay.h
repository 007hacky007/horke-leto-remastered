#pragma once
#include "data/font.h"
#include <optional>
namespace hl {
struct GlyphPlacement {uint8_t index{};int x{},y{};};
struct TextOverlay {int x{},y{},width{},height{};std::vector<uint8_t> mask;std::vector<GlyphPlacement> glyphs;};
Result<TextOverlay> make_text_overlay(const BitmapFont& font,std::span<const uint8_t> text,int x,int y);
struct ChoiceText {std::vector<uint8_t> text;std::vector<int> row_choices;};
Result<ChoiceText> make_choice_text(std::span<const uint8_t> text,uint32_t mask);
std::optional<int> choice_at(const ChoiceText&,const TextOverlay&,int x,int y);
std::optional<int> speech_animation_direction(int direction,bool& suppress);
}
