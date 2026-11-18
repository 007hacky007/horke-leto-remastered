#pragma once
#include "render/graphics_mode.h"
namespace hl {
enum class OptionAction {None,Graphics,Fullscreen,Size,Scaling,Hints,Close};
struct OptionRow {
 std::string label;int x{},y{},width{};OptionAction action{OptionAction::None};
 bool enabled{true},selected{};WindowSize size{};ScalingMode scaling{};
};
inline std::vector<OptionRow> options_rows(const GraphicsSettings& s,WindowSize usable){
 std::vector<OptionRow> rows{
  {std::string("Graphics: ")+(s.mode==GraphicsMode::Original?"[Original] / Remastered":"Original / [Remastered]"),64,112,512,OptionAction::Graphics},
  {std::string("Display: ")+(s.fullscreen?"Window / [Fullscreen]":"[Window] / Fullscreen"),64,144,512,OptionAction::Fullscreen},
  {"Window size:",64,176,512},
  {"Scaling:",64,240,512},
  {std::string("Active area hints: ")+(s.hints_enabled?"On":"Off")+" (hold "+s.hints_key+")",64,304,512,OptionAction::Hints},
  {"G / "+s.switch_key+": graphics",64,336,512},
  {"O / Esc: close",64,368,512,OptionAction::Close}
 };
#ifdef __APPLE__
 rows[2].label+=" (Cmd+Enter / Cmd+Ctrl+F: fullscreen)";
 rows[6].label="O / Cmd+comma / Esc: close";
#else
 rows[2].label+=" (Alt+Enter / F11: fullscreen)";
#endif
 int x=64;
 auto available=window_size_presets(usable.width,usable.height);
 for(auto size:{WindowSize{640,480},{960,720},{1280,960},{1600,1200},{1920,1440}}){
  rows.push_back({std::to_string(size.width)+"x"+std::to_string(size.height),x,208,102,OptionAction::Size,std::find(available.begin(),available.end(),size)!=available.end(),s.window_width==size.width&&s.window_height==size.height,size});x+=102;
 }
 x=64;for(auto mode:{ScalingMode::Fill,ScalingMode::Whole,ScalingMode::Stretch}){
  rows.push_back({mode==ScalingMode::Fill?"Fill 4:3":mode==ScalingMode::Whole?"Whole multiples":"Stretch to fill",x,272,x==404?172:170,OptionAction::Scaling,true,s.scaling==mode,{},mode});x+=170;
 }
 return rows;
}
inline std::optional<OptionRow> option_at(const std::vector<OptionRow>& rows,LogicalPoint p){
 for(const auto& row:rows)if(row.enabled&&row.action!=OptionAction::None&&p.x>=row.x&&p.x<row.x+row.width&&p.y>=row.y&&p.y<row.y+24)return row;
 return {};
}
}
