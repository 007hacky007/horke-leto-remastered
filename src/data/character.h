#pragma once
#include "data/result.h"
#include <array>
#include <vector>
namespace hl {
struct CharacterOffset {int16_t x{},y{};};
struct CharacterDirection {std::string file;int16_t speed{};std::vector<CharacterOffset> offsets;};
struct CharacterDefinition {std::array<CharacterDirection,8> directions;};
struct Region {std::string label;int16_t x{},y{};uint16_t flags{};std::vector<std::byte> shape;bool contains(int x,int y)const;};
Result<CharacterDefinition> load_character(ByteView bytes,std::string_view name);
Result<Region> load_region(ByteView bytes,std::string_view name);
}
