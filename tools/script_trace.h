#pragma once
#include "data/result.h"
#include <filesystem>
#include <nlohmann/json.hpp>
namespace hl {
Result<nlohmann::json> compare_dispatch_trace(const std::filesystem::path& reference,const std::filesystem::path& data);
}
int replay_trace_command(int argc,char** argv);
