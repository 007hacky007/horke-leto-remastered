#pragma once
#include "data/result.h"
#include <cstdint>
#include <vector>
namespace hl {
enum class GameEncoding {kamenicky};
struct TextControl {size_t byte_offset{},utf8_offset{};uint8_t code{};};
struct GameText {std::string utf8;std::vector<TextControl> controls;};
char32_t game_codepoint(uint8_t byte);
std::vector<std::byte> decrypt_game_text(ByteView bytes);
Result<GameText> parse_game_text(ByteView bytes,GameEncoding encoding);
Result<std::string> decode_game_text(ByteView bytes,GameEncoding encoding);
}
