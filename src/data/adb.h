#pragma once
#include "data/result.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>
namespace hl {
struct AdbEntry {std::string name;uint32_t relative_offset{},size{};};
class AdbDatabase {
 std::vector<std::byte> bytes_;std::vector<AdbEntry> entries_;std::map<std::string,size_t> lookup_;size_t base_{};uint32_t total_{},max_name_{};
public:
 static Result<AdbDatabase> open(const std::filesystem::path& path);
 static Result<AdbDatabase> parse(std::vector<std::byte> bytes);
 std::span<const AdbEntry> entries()const{return entries_;}
 size_t payload_base()const{return base_;}uint32_t total()const{return total_;}uint32_t max_name()const{return max_name_;}
 Result<ByteView> read(std::string_view name)const;
};
struct ObjectResource {std::string name;uint16_t flags{};};
struct ObjectLayout {uint16_t id{},code_size{};uint8_t flags{};size_t code_start{7},code_end{},table_offset{};uint16_t declared_table_offset{};std::vector<ObjectResource> resources;};
Result<ObjectLayout> inspect_object_header(ByteView bytes);
}
