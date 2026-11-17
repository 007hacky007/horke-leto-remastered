#pragma once
#include "data/disc.h"
#include <functional>
#include <array>
namespace hl {
struct ImportRequest {std::filesystem::path cue,directory;std::function<bool()> cancelled;std::array<std::filesystem::path,2> cd_tracks;bool rip_audio{};};
struct ImportedFile {std::string name;uint64_t size{};std::string source_sha256,destination_sha256;};
struct ImportManifest {std::string volume;std::vector<ImportedFile> files;bool audio_complete{};};
Result<ImportManifest> import_game(const ImportRequest& request,const std::filesystem::path& destination);
}
#include <nlohmann/json.hpp>
namespace hl {
Result<void> encode_flac(std::span<const int16_t>,uint32_t rate,uint16_t channels,const std::filesystem::path&);
Result<nlohmann::json> rip_cd(const DiscLayout&,const std::filesystem::path&);
}
