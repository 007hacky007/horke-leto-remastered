#include "data/font.h"
#include "data/byte_reader.h"
namespace hl {
Result<BitmapFont> load_original_font(const FontSource& source){
 if(!checked_range(source.bytes.size(),source.offset,4096))return DataError{"","font",source.offset,"truncated original 256 x 16-byte font"};BitmapFont out;
 constexpr char32_t low[]={0,0x263a,0x263b,0x2665,0x2666,0x2663,0x2660,0x2022,0x25d8,0x25cb,0x25d9,0x2642,0x2640,0x266a,0x266b,0x263c,0x25ba,0x25c4,0x2195,0x203c,0xb6,0xa7,0x25ac,0x21a8,0x2191,0x2193,0x2192,0x2190,0x221f,0x2194,0x25b2,0x25bc};
 for(unsigned i=0;i<256;++i){out.glyphs[i].codepoint=i<32?low[i]:i==127?0x2302:game_codepoint(uint8_t(i));for(unsigned y=0;y<16;++y)out.glyphs[i].rows[y]=std::to_integer<uint8_t>(source.bytes[source.offset+i*16+y]);}return out;
}
Result<std::vector<uint8_t>> render_font(const BitmapFont& f,std::span<const uint8_t> text){
 if(text.size()>2048||f.width!=8||f.height!=16)return DataError{"","font",0,"invalid font render dimensions"};size_t width=text.size()*8;std::vector<uint8_t> out(width*16);for(size_t i=0;i<text.size();++i)for(size_t y=0;y<16;++y)for(size_t x=0;x<8;++x)out[y*width+i*8+x]=(f.glyphs[text[i]].rows[y]&(128>>x))?255:0;return out;
}
}
