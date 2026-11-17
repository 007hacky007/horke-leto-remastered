#pragma once
#include "data/result.h"
#include <cstdint>
#include <istream>
#include <vector>
namespace hl {
struct InputEvent { std::uint64_t tick{}; std::string kind; int logical_x{}, logical_y{}; std::string key, button, text; };
Result<std::vector<InputEvent>> read_trace(std::istream& input);
Result<std::vector<InputEvent>> read_trace(const std::string& path);
}
