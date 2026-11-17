#include "render/palette.h"
#include "data/hash.h"
#include <cstdlib>
#include <algorithm>
namespace hl {
namespace {
uint8_t shade_luminance(unsigned luminance) {
 // DOS 0x16e92 and the fixed lookup at 0x4344c, strength 60 from 0x20eab.
 constexpr uint8_t gray[]{1,2,3,4,5,6,7,8,9,10,11,12,14,17,19,21,23,24,24,25,25,26,27,27,28,29,29,30,31,31,32,32};
 return gray[31-std::min(31u,luminance*60u/20400u)];
}
}
Result<PalettePostprocess> palette_postprocess(unsigned step,bool flash){
 if(step>64)return DataError{"","palette",step,"postprocess step outside 0..64"};float amount=float(step*63/64)/63.0f;return flash?PalettePostprocess{1-amount,amount}:PalettePostprocess{amount,0};
}
Result<std::string> frame_palette_identity(const IndexedFrame& frame,const Palette& live,std::optional<uint8_t> transparent){
 if(frame.palette==live)return sha256(std::as_bytes(std::span(live)));
 std::array<bool,256> used{};for(auto index:frame.indices)used[index]=true;
 for(size_t i=0;i<256;++i)if(used[i]&&(!transparent||i!=*transparent)&&frame.palette[i]!=live[i])return sha256(std::as_bytes(std::span(live)));
 return sha256(std::as_bytes(std::span(frame.palette)));
}
Result<PalettePostprocess> palette_postprocess(const PaletteView& view){
 if(!view.visible)return PalettePostprocess{0,0};
 double sx=0,sy=0,sxx=0,sxy=0;constexpr double count=256*3;
 for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c){double x=view.source[i][c]/4,y=view.current[i][c];sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
 double variance=sxx-sx*sx/count;double gain=variance?std::clamp((sxy-sx*sy/count)/variance,0.0,1.0):0;double bias=std::clamp((sy-gain*sx)/count,0.0,63.0)/63;
 if(gain+bias>1.02)return DataError{"","palette",0,"palette transform requires an offline variant"};
 for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c)if(std::abs(gain*(view.source[i][c]/4)+bias*63-view.current[i][c])>1.05)return DataError{"","palette",i,"palette transform requires an offline variant"};
 return PalettePostprocess{float(gain),float(std::min(bias,1-gain))};
}
uint8_t dialogue_shade_index(const Palette& palette,uint8_t index){auto p=palette[index];return shade_luminance(p[0]*30u+p[1]*59u+p[2]*11u);}
uint8_t dialogue_shade_source_index(const Palette& palette,uint8_t index){auto p=palette[index];return shade_luminance((p[0]*30u+p[1]*59u+p[2]*11u)/4);}
Result<Palette> interpolate_palette(const Palette& from,const Palette& to,unsigned step) {
 if(step>64)return DataError{"","palette",step,"DAC interpolation step outside 0..64"};Palette out;
 for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c) {
  if(from[i][c]>63||to[i][c]>63)return DataError{"","palette",i*3+c,"DAC component exceeds six bits"};
  int delta=int(to[i][c])-from[i][c];out[i][c]=uint8_t(from[i][c]+(delta<0?-1:1)*int(step*std::abs(delta)/64));
 }return out;
}
}
