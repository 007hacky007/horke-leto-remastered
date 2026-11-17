#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include "data/result.h"
namespace hl {
struct Checkpoint {
 std::uint64_t tick{}; std::string scene;
 nlohmann::json variables=nullptr,inventory=nullptr,active_dialogue=nullptr,animation_positions=nullptr,audio_positions=nullptr;
};
Result<nlohmann::json> compare_checkpoint(const nlohmann::json& reference,const nlohmann::json& native);
std::string canonical_checkpoint(const Checkpoint& checkpoint);
}
