#pragma once
#include "data/result.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include "world/inventory.h"
#include <map>
namespace hl {
struct DosSceneState {bool visited{};std::string region;std::map<std::string,int16_t> members;};
struct DosSaveState {
 std::string scene,character_file;int x{},y{},direction{},movement_depth{};bool character_visible{};
 std::map<std::string,int32_t> globals,integers;
 std::map<std::string,std::string> strings,regions;
 std::map<std::string,DosSceneState> scenes;
 std::array<InventorySlot,35> inventory;
 std::array<std::array<uint8_t,3>,256> palette{};
};
Result<DosSaveState> parse_dos_save(ByteView);
struct SaveState {std::string data_identity;nlohmann::json reconstruction;};
Result<void> save_game(const SaveState&,const std::filesystem::path&);
Result<SaveState> load_game(const std::filesystem::path&,std::string_view identity);
}
