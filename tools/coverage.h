#pragma once
#include "data/result.h"
#include "assets.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <set>
namespace hl {
struct CoverageState {std::map<std::string,uint32_t> expected;std::map<std::string,std::set<uint32_t>> decoded,exported;std::set<std::string> wav_expected,wav_decoded,exception_names;std::set<unsigned> audio_tracks;uint32_t wav_errors{};bool text_font_valid{},transparency_valid{};};
Result<void> validate_frame_export(const IndexedFrame&,const nlohmann::json&,const nlohmann::json&,const CroppedFrame&,std::span<const uint8_t>,const TransparencyRule&);
Result<void> validate_transparency_evidence(const nlohmann::json&);
Result<nlohmann::json> validate_coverage(const CoverageState&);
Result<nlohmann::json> verify_assets(const std::filesystem::path& data,const std::filesystem::path& frames);
}
