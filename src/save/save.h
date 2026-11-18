#pragma once
#include "data/result.h"
#include <filesystem>
#include "world/inventory.h"
#include <array>
#include <map>
#include <span>
#include <vector>
namespace hl {
// One visited screen as DOS 0x2c19a records it when the screen is left or saved.
struct DosSceneState {bool visited{};std::string region;std::map<std::string,int16_t> members;
 std::string entered_from,cursor;int32_t screen{},x{},y{},direction{},actor_visible{};std::array<std::array<uint8_t,3>,256> palette{};};
struct DosSaveState {
 std::string scene,character_file;int x{},y{},direction{},movement_depth{};bool character_visible{};
 std::map<std::string,int32_t> globals,integers;
 std::map<std::string,std::string> strings,regions;
 std::map<std::string,DosSceneState> scenes;
 std::array<InventorySlot,35> inventory;
 std::array<std::array<uint8_t,3>,256> palette{};
};
Result<DosSaveState> parse_dos_save(ByteView);
Result<std::vector<std::byte>> write_dos_save(const DosSaveState&);
// Slot files follow the original: SAVE.<slot> plus ten 51-byte names in SAVNAMES.TXT.
std::filesystem::path dos_save_path(const std::filesystem::path& directory,int slot);
Result<void> write_file_atomic(const std::filesystem::path&,std::span<const std::byte>);
std::array<std::vector<uint8_t>,10> read_save_names(const std::filesystem::path& directory);
Result<void> write_save_names(const std::filesystem::path& directory,const std::array<std::vector<uint8_t>,10>& names);
}
