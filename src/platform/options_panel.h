#pragma once
#include "render/graphics_mode.h"
namespace hl {
enum class OptionAction {None,Graphics,Fullscreen,Size,Scaling,Hints,Inventory,Close,Help};
struct OptionRow {
 std::string label;int x{},y{},width{};OptionAction action{OptionAction::None};
 bool enabled{true},selected{};WindowSize size{};ScalingMode scaling{};int height{24};
};
inline std::vector<OptionRow> options_rows(const GraphicsSettings& s,WindowSize usable){
 std::vector<OptionRow> rows{
  {std::string("Grafika: ")+(s.mode==GraphicsMode::Original?"[Original] / Remastered":"Original / [Remastered]"),64,112,512,OptionAction::Graphics},
  {std::string("Zobrazení: ")+(s.fullscreen?"Okno / [Celá obrazovka]":"[Okno] / Celá obrazovka"),64,144,512,OptionAction::Fullscreen},
  {"Velikost okna:",64,176,512},
  {"Škálování:",64,240,512},
  {std::string("Nápověda míst: ")+(s.hints_enabled?"zapnuto":"vypnuto")+" (podrž "+(s.hints_key=="Space"?std::string("mezerník"):s.hints_key)+")",64,304,512,OptionAction::Hints},
  {std::string("Ikona inventáře: ")+(s.inventory_icon?"zapnuto":"vypnuto")+" (I / Tab: inventář)",64,336,512,OptionAction::Inventory},
  {"G / "+s.switch_key+": grafika",64,368,512},
  {"O / Esc: zavřít",64,400,512,OptionAction::Close}
 };
#ifdef __APPLE__
 rows[2].label+=" (Cmd+Enter / Cmd+Ctrl+F: celá obrazovka)";
 rows[7].label="O / Cmd+, / Esc: zavřít";
#else
 rows[2].label+=" (Alt+Enter / F11: celá obrazovka)";
#endif
 int x=64;
 auto available=window_size_presets(usable.width,usable.height);
 for(auto size:{WindowSize{640,480},{960,720},{1280,960},{1600,1200},{1920,1440}}){
  rows.push_back({std::to_string(size.width)+"x"+std::to_string(size.height),x,208,102,OptionAction::Size,std::find(available.begin(),available.end(),size)!=available.end(),s.window_width==size.width&&s.window_height==size.height,size});x+=102;
 }
 x=64;for(auto mode:{ScalingMode::Fill,ScalingMode::Whole,ScalingMode::Stretch}){
  rows.push_back({mode==ScalingMode::Fill?"Vyplnit 4:3":mode==ScalingMode::Whole?"Celé násobky":"Roztáhnout",x,272,x==404?172:170,OptionAction::Scaling,true,s.scaling==mode,{},mode});x+=170;
 }
 return rows;
}
inline std::optional<OptionRow> option_at(const std::vector<OptionRow>& rows,LogicalPoint p){
 for(const auto& row:rows)if(row.enabled&&row.action!=OptionAction::None&&p.x>=row.x&&p.x<row.x+row.width&&p.y>=row.y&&p.y<row.y+row.height)return row;
 return {};
}
}
