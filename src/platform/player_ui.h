#pragma once
#include "platform/options_panel.h"
#include "data/font.h"
#include "render/scene_graph.h"
namespace hl {
std::string ui_text(const BitmapFont&,std::u32string_view);
std::vector<OptionRow> game_settings_rows(const GraphicsSettings&,WindowSize,const BitmapFont&);
std::vector<RenderSurface> game_settings_surfaces(const std::vector<OptionRow>&,const BitmapFont&);
// Our own pixel-art bag for the top-left inventory hotspot (empty when the corner is too small).
std::vector<RenderSurface> inventory_icon_surfaces(int corner);
std::vector<RenderSurface> help_surfaces(const BitmapFont&,uint8_t alpha,const GraphicsSettings& = {});
class HelpCard {
 uint64_t start_{},duration_{};bool visible_{};
public:
 void launch(GraphicsControl& graphics,uint64_t now){show(now,graphics.record_launch()<=3?6000:2000);}
 void show(uint64_t now,uint64_t duration=6000){start_=now;duration_=duration;visible_=true;}
 void dismiss(){visible_=false;}
 uint8_t alpha(uint64_t now)const{
  if(!visible_||now<start_||now-start_>=duration_)return 0;
  auto age=now-start_;return uint8_t(std::min({uint64_t(255),age*255/250,(duration_-age)*255/250}));
 }
 bool visible(uint64_t now)const{return visible_&&now>=start_&&now-start_<duration_;}
};
}
