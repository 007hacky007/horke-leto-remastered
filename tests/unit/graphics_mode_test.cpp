#include <doctest/doctest.h>
#include <fstream>
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
TEST_SUITE("render.graphics_mode") {
TEST_CASE("window preferences round trip and old settings keep automatic sizing") {
 auto file=std::filesystem::temp_directory_path()/"hl-window-settings.json";
 GraphicsSettings s;s.fullscreen=true;s.window_width=1280;s.window_height=960;
 REQUIRE(save_graphics_settings(file,s).has_value());auto loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());
 CHECK(loaded.value().fullscreen);CHECK(loaded.value().window_width==1280);CHECK(loaded.value().window_height==960);
 CHECK(graphics_menu(s).find("F11")!=std::string::npos);
 {std::ofstream out(file);out<<R"({"schema_version":1,"graphics":"Original"})";}
 loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());CHECK_FALSE(loaded.value().fullscreen);CHECK(loaded.value().window_width==0);CHECK(loaded.value().window_height==0);
 for(auto size:std::vector<std::pair<int,int>>{{-1,480},{640,0},{0,480}}){s.window_width=size.first;s.window_height=size.second;CHECK_FALSE(save_graphics_settings(file,s).has_value());
  std::ofstream out(file);out<<nlohmann::json{{"schema_version",1},{"graphics","Original"},{"window_width",size.first},{"window_height",size.second}};out.close();CHECK_FALSE(load_graphics_settings(file).has_value());}
 std::filesystem::remove(file);
}
TEST_CASE("startup chooses the largest integer scale in usable display coordinates") {
 CHECK(choose_window_size(1920,1040,{})==WindowSize{1280,960});
 CHECK(choose_window_size(2560,1440,{})==WindowSize{1920,1440});
 CHECK(choose_window_size(1280,960,{})==WindowSize{1280,960});
 CHECK(choose_window_size(1280,959,{})==WindowSize{640,480});
 CHECK(choose_window_size(800,600,{})==WindowSize{640,480});
 CHECK(choose_window_size(600,400,{})==WindowSize{600,400});
 GraphicsSettings s;s.window_width=1000;s.window_height=700;
 CHECK(choose_window_size(1920,1040,s)==WindowSize{1000,700});
 CHECK(choose_window_size(800,600,s)==WindowSize{800,600});
}
}
TEST_SUITE("render.graphics_mode") {
TEST_CASE("scaling modes round trip legacy settings default to fill and invalid modes fail") {
 auto file=std::filesystem::temp_directory_path()/"hl-scaling-settings.json";
 for(auto mode:{ScalingMode::Fill,ScalingMode::Whole,ScalingMode::Stretch}){
  GraphicsSettings s;s.scaling=mode;REQUIRE(save_graphics_settings(file,s).has_value());auto r=load_graphics_settings(file);REQUIRE(r.has_value());CHECK(r.value().scaling==mode);
 }
 {std::ofstream out(file);out<<R"({"schema_version":1,"graphics":"Original"})";}
 auto old=load_graphics_settings(file);REQUIRE(old.has_value());CHECK(old.value().scaling==ScalingMode::Fill);
 {std::ofstream out(file);out<<R"({"schema_version":1,"graphics":"Original","scaling":"bogus"})";}
 CHECK_FALSE(load_graphics_settings(file).has_value());std::filesystem::remove(file);
}
TEST_CASE("window presets exclude every size exceeding usable display bounds") {
 CHECK(window_size_presets(1512,900)==std::vector<WindowSize>{{640,480},{960,720}});
 CHECK(window_size_presets(1920,1440)==std::vector<WindowSize>{{640,480},{960,720},{1280,960},{1600,1200},{1920,1440}});
 CHECK(window_size_presets(639,480).empty());CHECK(window_size_presets(640,479).empty());
}
TEST_CASE("the inventory icon setting persists and defaults to shown") {
 auto file=std::filesystem::path("work/m5/settings-inventory-test.json");GraphicsSettings s;s.inventory_icon=false;
 REQUIRE(save_graphics_settings(file,s).has_value());auto loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());CHECK_FALSE(loaded.value().inventory_icon);
 {std::ofstream out(file);out<<R"({"schema_version":1,"graphics":"Original"})";}
 loaded=load_graphics_settings(file);REQUIRE(loaded.has_value());CHECK(loaded.value().inventory_icon);std::filesystem::remove(file);
}
}
