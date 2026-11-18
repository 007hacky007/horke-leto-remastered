#include "platform/player_ui.h"
namespace hl {
namespace {
std::optional<uint8_t> glyph(const BitmapFont& font,char32_t c){
 for(unsigned i=32;i<256;++i)if(font.glyphs[i].codepoint==c&&(c==U' '||std::any_of(font.glyphs[i].rows.begin(),font.glyphs[i].rows.end(),[](auto row){return row!=0;})))return uint8_t(i);
 return {};
}
char32_t ascii(char32_t c){
 constexpr std::u32string_view accents=U"áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ";
 constexpr std::u32string_view plain=U"acdeeinorstuuyzACDEEINORSTUUYZ";
 auto pos=accents.find(c);return pos==accents.npos?c:plain[pos];
}
RenderSurface text_surface(const BitmapFont& font,std::string_view bytes,int x,int y,int z,std::array<uint8_t,3> color,uint8_t alpha=255){
 std::vector<uint8_t> text(bytes.begin(),bytes.end());auto mask=render_font(font,text).value();RenderSurface surface{x,y,z,int(bytes.size())*8,16,{}};
 for(auto bit:mask)for(auto value:{color[0],color[1],color[2],uint8_t(bit?alpha:0)})surface.rgba.push_back(value);
 return surface;
}
RenderSurface rectangle(int x,int y,int w,int h,int z,std::array<uint8_t,4> color){
 RenderSurface surface{x,y,z,w,h,{}};for(int i=0;i<w*h;++i)surface.rgba.insert(surface.rgba.end(),color.begin(),color.end());return surface;
}
}
std::string ui_text(const BitmapFont& font,std::u32string_view text){
 std::string out;
 for(size_t begin=0;begin<text.size();){
  auto end=text.find(U' ',begin);if(end==text.npos)end=text.size();auto word=text.substr(begin,end-begin);
  bool fallback=std::any_of(word.begin(),word.end(),[&](auto c){return !glyph(font,c);});
  for(auto c:word)out+=char(glyph(font,fallback?ascii(c):c).value_or('?'));
  if(end<text.size())out+=' ';begin=end+1;
 }
 return out;
}
std::vector<OptionRow> game_settings_rows(const GraphicsSettings& s,WindowSize usable,const BitmapFont& font){
 std::vector<OptionRow> rows;
 auto row=[&](std::u32string_view text,int x,int y,int width,OptionAction action){rows.push_back({ui_text(font,text),x,y,width,action});rows.back().height=20;};
 row(s.mode==GraphicsMode::Original?U"Grafika: [Original] / Remastered":U"Grafika: Original / [Remastered]",16,0,320,OptionAction::Graphics);
 row(s.fullscreen?U"Celá obrazovka: [ano] / ne":U"Celá obrazovka: ano / [ne]",336,0,288,OptionAction::Fullscreen);
 // Keep every added pixel outside the DOS controls, including their label ascenders.
 row(U"Velikost okna:",16,20,128,OptionAction::None);
 int x=144;for(auto size:window_size_presets(usable.width,usable.height)){
  std::string label=std::to_string(size.width)+"x"+std::to_string(size.height);if(size.width==s.window_width&&size.height==s.window_height)label="["+label+"]";
  rows.push_back({label,x,20,96,OptionAction::Size,true,false,size,{},20});x+=96;
 }
 row(U"Škálování:",16,40,128,OptionAction::None);x=144;
 for(auto mode:{ScalingMode::Fill,ScalingMode::Whole,ScalingMode::Stretch}){
  auto label=ui_text(font,mode==ScalingMode::Fill?U"vyplnit 4:3":mode==ScalingMode::Whole?U"celé násobky":U"roztáhnout");if(mode==s.scaling)label="["+label+"]";
  rows.push_back({label,x,40,160,OptionAction::Scaling,true,false,{},mode,20});x+=160;
 }
 row(s.hints_enabled?U"Nápověda míst (mezerník): [ano] / ne":U"Nápověda míst (mezerník): ano / [ne]",16,60,296,OptionAction::Hints);
 if(s.hints_key!="Space")rows.back().label=ui_text(font,U"Nápověda míst (")+s.hints_key+ui_text(font,s.hints_enabled?U"): [ano] / ne":U"): ano / [ne]");
 row(s.inventory_icon?U"Ikona inventáře: [ano] / ne":U"Ikona inventáře: ano / [ne]",320,60,304,OptionAction::Inventory);
 row(U"Klávesy: Esc menu | I/Tab inventář | G grafika | O nastavení | H/? nápověda",16,456,608,OptionAction::Help);
 return rows;
}
std::vector<RenderSurface> game_settings_surfaces(const std::vector<OptionRow>& rows,const BitmapFont& font){
 std::vector<RenderSurface> out;for(const auto& row:rows)out.push_back(text_surface(font,row.label,row.x,row.y,-95,{255,65,0}));return out;
}
std::vector<RenderSurface> inventory_icon_surfaces(int corner){
 if(corner<14)return {};
 static constexpr const char* art[12]={
  "....####....",
  "...#....#...",
  "..########..",
  ".#OOOOOOOO#.",
  ".#FFFFFFFF#.",
  ".#FFFFFFFF#.",
  ".#OOOBBOOO#.",
  ".#OOOBBOOO#.",
  ".#OPPPPPPO#.",
  ".#OPOOOOPO#.",
  ".#OPPPPPPO#.",
  "..########.."};
 RenderSurface plate=rectangle(0,0,14,14,-90,{0,0,0,96});
 for(int c:{0,13})for(int r:{0,13})std::fill_n(plate.rgba.begin()+(r*14+c)*4,4,uint8_t(0));
 RenderSurface bag{1,1,-91,12,12,{}};
 for(int y=0;y<12;++y)for(int x=0;x<12;++x){
  std::array<uint8_t,4> c{0,0,0,0};
  switch(art[y][x]){case '#':c={44,24,10,255};break;case 'O':c={182,124,66,255};break;case 'F':c={136,86,42,255};break;case 'B':c={236,198,70,255};break;case 'P':c={112,70,32,255};break;default:break;}
  bag.rgba.insert(bag.rgba.end(),c.begin(),c.end());
 }
 return {std::move(plate),std::move(bag)};
}
std::vector<RenderSurface> help_surfaces(const BitmapFont& font,uint8_t alpha,const GraphicsSettings& settings){
 if(!alpha)return {};
 std::vector<RenderSurface> out{rectangle(128,16,384,176,-95,{255,65,0,alpha}),rectangle(130,18,380,172,-96,{24,8,0,alpha})};
 const std::u32string_view lines[]={U"Klávesy",U"Esc menu    O nastavení    H / ? nápověda",U"I / Tab inventář (nebo levý horní roh)",U"F2 uložit hru    F3 nahrát hru",U"G grafika Original/Remastered",U"mezerník aktivní místa",
#ifdef __APPLE__
 U"Cmd+Ctrl+F celá obrazovka",
#else
 U"F11 / Alt+Enter celá obrazovka",
#endif
 U"Kliknutím zavřít"};
 int y=24;for(auto line:lines){
  auto text=ui_text(font,line);if(line==U"mezerník aktivní místa"&&settings.hints_key!="Space")text=settings.hints_key+ui_text(font,U" aktivní místa");
  out.push_back(text_surface(font,text,144,y,-97,{255,195,130},alpha));y+=20;
 }return out;
}
}
