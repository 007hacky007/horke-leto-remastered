#pragma once
#include "data/result.h"
#include <cstdint>
#include <string_view>
namespace hl {
struct AppOptions { bool smoke_test = false; std::uint32_t frames = 3; std::string report_path; std::string capture_path; std::string data_path,replay_path,checkpoint_out,opcode_trace_path,load_save;std::string renderer;std::string original_pack="packs/Original",remastered_pack,graphics_settings,save_directory; };
Result<AppOptions> parse_app_options(std::span<const std::string_view> args);
int run_native_app(const AppOptions& options);
int run_app(const AppOptions& options);
}
