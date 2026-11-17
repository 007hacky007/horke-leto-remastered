#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>
namespace hl {
struct DiscTrack {unsigned number{};bool mode1{};uint64_t index01{};std::optional<uint64_t> index00;uint64_t pregap{};};
struct DiscLayout {std::filesystem::path bin;std::vector<DiscTrack> tracks;uint64_t sectors{};};
Result<DiscLayout> parse_cue(std::string_view text);
bool safe_relative_path(const std::filesystem::path& path);
class DiscReader {
 DiscLayout layout_;
public:
 static Result<DiscReader> open(const std::filesystem::path& cue);
 const DiscLayout& layout()const{return layout_;}
 uint64_t data_sectors()const;
 Result<std::array<std::byte,2048>> read_mode1_sector(uint64_t lba)const;
};
}
namespace hl {
struct IsoExtent {uint32_t lba{},size{};};
struct IsoEntry {std::string name;IsoExtent extent;bool directory{};};
Result<void> validate_iso_extent(IsoExtent extent,uint64_t data_sectors);
Result<std::vector<IsoEntry>> parse_iso_directory(ByteView bytes,uint64_t data_sectors);
Result<std::vector<std::byte>> read_iso_extent(DiscReader& disc,IsoExtent extent);
Result<std::vector<IsoEntry>> read_iso_directory(DiscReader& disc,IsoExtent extent);
struct IsoVolume {std::string name;IsoExtent root;};
Result<IsoVolume> read_iso_volume(DiscReader& disc);
}
namespace hl {
struct CdRange {unsigned track{};uint64_t begin{},end{};bool pregap{};};
Result<std::vector<CdRange>> cd_track_ranges(const DiscLayout&);
Result<std::vector<int16_t>> extract_cd_pcm(const DiscLayout&,const CdRange&);
}
