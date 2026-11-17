#include "render/text_overlay.h"
#include <algorithm>
namespace hl {
Result<ChoiceText> make_choice_text(std::span<const uint8_t> text,uint32_t mask) {
 if(text.empty()||text.size()>4096)return DataError{"","choices",0,"invalid original text extent"};
 ChoiceText out;int id=0;bool line=false;
 auto enabled=[&]{return !mask||(mask&(uint32_t(1)<<(unsigned(id)&31)));};
 for(size_t i=0;i<text.size()&&text[i];++i){auto c=text[i];if(c==13)continue;
  if(c==';'||c==10){if(enabled()){out.row_choices.push_back(id);out.text.push_back(10);line=false;}
   if(c==';'){++id;if(i+1<text.size()&&text[i+1]==13)++i;if(i+1<text.size()&&text[i+1]==10)++i;}
  }else {if(c<32)return DataError{"","choices",i,"unsupported text control"};if(enabled()){out.text.push_back(c);line=true;}}
  if(out.row_choices.size()>40)return DataError{"","choices",i,"too many choice rows"};
 }
 if(line)out.row_choices.push_back(id);
 if(out.row_choices.size()>40)return DataError{"","choices",0,"too many choice rows"};
 if(out.row_choices.empty())return DataError{"","choices",0,"no enabled choice rows"};
 if(!out.text.empty()&&out.text.back()==10)out.text.pop_back();out.text.push_back(0);return out;
}
std::optional<int> choice_at(const ChoiceText& choices,const TextOverlay& box,int x,int y) {
 int row=(y-box.y-4)/16;
 if(x<box.x+4||x>=box.x+box.width-4||y<box.y+4||row<0||size_t(row)>=choices.row_choices.size())return {};
 return choices.row_choices[size_t(row)];
}
std::optional<int> speech_animation_direction(int direction,bool& suppress) {
 // DOS316ed/30983 consumes suppression only for ordinary speech.
 if(suppress){suppress=false;return {};}
 if(direction==2||direction==3)return 7;if(direction==4)return 3;if(direction==5||direction==6)return 5;return {};
}
Result<TextOverlay> make_text_overlay(const BitmapFont& font,std::span<const uint8_t> text,int x,int y) {
 if(text.empty()||text.size()>4096)return DataError{"","dialogue",0,"invalid original text extent"};
 std::vector<std::vector<uint8_t>> lines(1);for(auto c:text){if(c==0)break;if(c==13)continue;if(c==10||c==';')lines.emplace_back();else if(c<32)return DataError{"","dialogue",0,"unsupported text control"};else lines.back().push_back(c);}
 if(lines.back().empty())lines.pop_back();if(lines.empty())return DataError{"","dialogue",0,"empty original text"};
 size_t columns=0;for(const auto& line:lines)columns=std::max(columns,line.size());if(columns>79||lines.size()>29)return DataError{"","dialogue",0,"text exceeds logical canvas"};
 TextOverlay out{x-4,y-4,int(columns*8+8),int(lines.size()*16+8),{}};out.mask.resize(size_t(out.width)*out.height);
 for(size_t row=0;row<lines.size();++row){auto pixels=render_font(font,lines[row]);if(!pixels.has_value())return pixels.error();size_t width=lines[row].size()*8,left=4;
  for(size_t col=0;col<lines[row].size();++col)out.glyphs.push_back({lines[row][col],x+int(col)*8,y+int(row)*16});
  for(size_t py=0;py<16;++py)for(size_t px=0;px<width;++px)out.mask[(row*16+py+4)*size_t(out.width)+px+left]=pixels.value()[py*width+px];
 }return out;
}
}
