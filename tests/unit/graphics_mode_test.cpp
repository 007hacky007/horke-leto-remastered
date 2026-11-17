#include <doctest/doctest.h>
#include "render/graphics_mode.h"
using namespace hl;
TEST_SUITE("render.graphics_mode") {
TEST_CASE("F8 and remapped keys queue changes until the next frame and ignore repeats") {GraphicsSettings s;GraphicsControl c(s);CHECK(c.mode()==GraphicsMode::Original);CHECK(c.key("F8",true,false));CHECK(c.mode()==GraphicsMode::Original);c.frame_boundary();CHECK(c.mode()==GraphicsMode::Remastered);c.key("F8",true,true);c.frame_boundary();CHECK(c.mode()==GraphicsMode::Remastered);s.switch_key="F9";GraphicsControl remapped(s);CHECK_FALSE(remapped.key("F8",true,false));CHECK(remapped.key("F9",true,false));remapped.frame_boundary();CHECK(remapped.mode()==GraphicsMode::Remastered);}
TEST_CASE("hints are held remappable disabled on release and focus loss") {GraphicsSettings s;s.hints_key="H";GraphicsControl c(s);CHECK_FALSE(c.key("Space",true,false));c.key("H",true,false);CHECK(c.hints_visible());c.key("H",false,false);CHECK_FALSE(c.hints_visible());c.key("H",true,false);c.focus_lost();CHECK_FALSE(c.hints_visible());c.set_hints_enabled(false);c.key("H",true,false);CHECK_FALSE(c.hints_visible());}
TEST_CASE("settings persist outside saves and menu keeps developer nearest hidden") {auto file=std::filesystem::path("work/m5/settings-test.json");GraphicsSettings s;s.mode=GraphicsMode::Remastered;s.switch_key="F9";s.hints_key="H";s.hints_enabled=false;s.developer_nearest=true;REQUIRE(save_graphics_settings(file,s).has_value());auto loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());CHECK(loaded.value().mode==GraphicsMode::Remastered);CHECK(loaded.value().switch_key=="F9");CHECK_FALSE(loaded.value().hints_enabled);CHECK(loaded.value().developer_nearest);auto text=graphics_menu(loaded.value());CHECK(text.find("Graphics: Original / Remastered")!=std::string::npos);CHECK(text.find("Switch graphics: F9")!=std::string::npos);CHECK(text.find("Show active areas: hold H")!=std::string::npos);CHECK(text.find("nearest")==std::string::npos);CHECK_FALSE(load_graphics_settings(file.parent_path()/"missing.json").has_value());}
}
TEST_SUITE("render.graphics_mode") {
TEST_CASE("renderer policy persists and validates config") {
 auto file=std::filesystem::temp_directory_path()/"hl-renderer-settings.json";
 GraphicsSettings s;s.renderer="software";REQUIRE(save_graphics_settings(file,s).has_value());
 auto loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());CHECK(loaded.value().renderer=="software");
 s.renderer="invalid";CHECK_FALSE(save_graphics_settings(file,s).has_value());
 std::filesystem::remove(file);
}
}
TEST_SUITE("render.graphics_mode") {
TEST_CASE("an early F8 waits for ready textures without discarding the requested mode") {
 GraphicsControl c({});c.key("F8",true,false);
 CHECK_FALSE(c.frame_boundary(false));CHECK(c.mode()==GraphicsMode::Original);
 CHECK(c.frame_boundary(true));CHECK(c.mode()==GraphicsMode::Remastered);
 c.key("F8",true,false);c.key("F8",true,false);
 CHECK_FALSE(c.frame_boundary());CHECK(c.mode()==GraphicsMode::Remastered);
}
}
