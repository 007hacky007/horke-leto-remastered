#pragma once
#include <nlohmann/json.hpp>
#include <vector>
namespace hl {bool original_rate_budget_passes(const std::vector<double>& wall);nlohmann::json timing_summary(std::vector<double>);bool accelerated_budget_passes(const std::vector<double>& cpu,const std::vector<double>& gpu);}
int benchmark_command(int argc,char** argv);
