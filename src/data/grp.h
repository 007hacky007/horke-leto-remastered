#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>
namespace hl {
std::string ascii_key(std::string_view name);
Result<std::string> archive_name(ByteView bytes,bool terminator_required);
Result<std::vector<std::byte>> read_file(const std::filesystem::path& path);
struct GrpEntry {std::string name;uint32_t offset{},size{};std::array<std::byte,8> metadata{};};
class GrpArchive {
 std::vector<std::byte> bytes_;std::vector<GrpEntry> entries_;std::map<std::string,size_t> lookup_;uint32_t capacity_{};
public:
 static Result<GrpArchive> open(const std::filesystem::path& path);
 static Result<GrpArchive> parse(std::vector<std::byte> bytes);
 std::span<const GrpEntry> entries()const{return entries_;}
 uint32_t capacity()const{return capacity_;}
 Result<std::vector<std::byte>> read(std::string_view name)const;
};
}
