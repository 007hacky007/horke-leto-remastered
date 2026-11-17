#include "render/graphics_mode.h"
#include "render/renderer_policy.h"
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace hl {
namespace {
bool valid_key(const std::string& key){return !key.empty()&&key.size()<=32&&std::all_of(key.begin(),key.end(),[](unsigned char c){return c>=32&&c<127;});}
}
Result<GraphicsSettings> load_graphics_settings(const std::filesystem::path& path){
 try{std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>16384)return DataError{"","graphics settings",0,"missing or excessive settings file"};std::ifstream input(path);auto j=nlohmann::json::parse(input);if(j.at("schema_version")!=1)return DataError{"","graphics settings",0,"unsupported settings version"};GraphicsSettings out;auto mode=j.at("graphics").get<std::string>();if(mode!="Original"&&mode!="Remastered")return DataError{"","graphics settings",0,"invalid graphics mode"};out.renderer=j.value("renderer","auto");if(!valid_renderer_preference(out.renderer))return DataError{"","graphics settings",0,"invalid renderer preference"};out.mode=mode=="Original"?GraphicsMode::Original:GraphicsMode::Remastered;out.switch_key=j.value("switch_graphics","F8");out.hints_key=j.value("show_active_areas","Space");out.hints_enabled=j.value("active_area_hints",true);out.developer_nearest=j.value("developer_nearest",false);if(!valid_key(out.switch_key)||!valid_key(out.hints_key)||out.switch_key==out.hints_key)return DataError{"","graphics settings",0,"invalid or conflicting key binding"};return out;
 }catch(const std::exception& e){return DataError{"","graphics settings",0,e.what()};}
}
Result<void> save_graphics_settings(const std::filesystem::path& path,const GraphicsSettings& s){
 if(!valid_renderer_preference(s.renderer))return DataError{"","graphics settings",0,"invalid renderer preference"};
 if(!valid_key(s.switch_key)||!valid_key(s.hints_key)||s.switch_key==s.hints_key)return DataError{"","graphics settings",0,"invalid or conflicting key binding"};
 auto temporary=path;temporary+=".tmp";std::error_code ec;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),ec);if(ec)return DataError{"","graphics settings",0,"cannot create settings directory"};
 {std::ofstream out(temporary);out<<nlohmann::json{{"schema_version",1},{"renderer",s.renderer},{"graphics",s.mode==GraphicsMode::Original?"Original":"Remastered"},{"switch_graphics",s.switch_key},{"show_active_areas",s.hints_key},{"active_area_hints",s.hints_enabled},{"developer_nearest",s.developer_nearest}}.dump(2)<<'\n';out.close();if(!out)return DataError{"","graphics settings",0,"cannot write settings"};}
#ifdef _WIN32
 if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return DataError{"","graphics settings",0,"cannot replace settings"};
#else
 std::filesystem::rename(temporary,path,ec);if(ec)return DataError{"","graphics settings",0,"cannot replace settings"};
#endif
 return {};
}
std::string graphics_menu(const GraphicsSettings& s){return "Graphics: Original / Remastered\nSwitch graphics: "+s.switch_key+"\nShow active areas: hold "+s.hints_key;}
bool GraphicsControl::key(std::string_view key,bool down,bool repeat){if(key==settings_.hints_key){held_=down;return true;}if(key==settings_.switch_key){if(down&&!repeat)toggle();return true;}return false;}
}
