#include <doctest/doctest.h>
#include "render/palette.h"
#include "data/hash.h"
#include <cstring>
#include <algorithm>
#include <cmath>
using namespace hl;
TEST_SUITE("render.palette") {
TEST_CASE("64 DAC steps match the original accumulator in both directions") {
 Palette a{},b{};a[1]={63,0,32};b[1]={0,63,32};
 CHECK(interpolate_palette(a,b,0).value()==a);CHECK(interpolate_palette(a,b,64).value()==b);
 for(unsigned k=0;k<=64;++k){auto p=interpolate_palette(a,b,k).value();CHECK(p[1][0]==63-int(k*63/64));CHECK(p[1][1]==int(k*63/64));CHECK(p[1][2]==32);}
 CHECK_FALSE(interpolate_palette(a,b,65).has_value());a[0][0]=64;CHECK_FALSE(interpolate_palette(a,b,1).has_value());
}
}
TEST_SUITE("render.palette") {
TEST_CASE("six bit VGA DAC colors expand to the DOSBox RGB levels") {
 CHECK(hl::expand_dac(0)==0);CHECK(hl::expand_dac(18)==73);CHECK(hl::expand_dac(39)==158);CHECK(hl::expand_dac(63)==255);
}

}
TEST_SUITE("render.palette") {
TEST_CASE("DOS text background uses the original grayscale index lookup") {
 Palette p{};CHECK(dialogue_shade_index(p,0)==32);p[1]={63,63,63};CHECK(dialogue_shade_index(p,1)==17);
}
}
TEST_SUITE("render.palette") {
TEST_CASE("dialogue luminance is computed before DAC channel quantization") {hl::Palette full{};full[152]={73,2,45};CHECK(hl::dialogue_shade_source_index(full,152)==31);hl::Palette dac{};dac[152]={18,0,11};CHECK(hl::dialogue_shade_index(dac,152)==32);}
}
TEST_SUITE("render.palette") {
TEST_CASE("live palette view produces postprocess only for representable fades and flashes") {hl::PaletteView view;for(auto& c:view.source)c={252,128,0};for(auto& c:view.current)c={31,16,0};auto p=hl::palette_postprocess(view);REQUIRE(p.has_value());CHECK(p.value().gain==doctest::Approx(.5).epsilon(.03));view.visible=false;p=hl::palette_postprocess(view);REQUIRE(p.has_value());CHECK(p.value().gain==0);CHECK(p.value().bias==0);}
}
TEST_SUITE("render.palette") {
TEST_CASE("unused palette differences alias safely and visible colour changes require variants") {hl::IndexedFrame frame;frame.indices={0,1,1};frame.palette[1]={100,80,40};hl::Palette live=frame.palette;live[2]={255,0,0};CHECK(hl::frame_palette_identity(frame,live,0).has_value());live[1]={0,0,0};auto variant=hl::frame_palette_identity(frame,live,0);REQUIRE(variant.has_value());CHECK(variant.value()==hl::sha256(std::as_bytes(std::span(live))));}
}

TEST_SUITE("render.palette") {
TEST_CASE("texture content hashes preserve standard SHA256 at padding boundaries") {
 for(auto [text,expected]:{std::pair{"","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},std::pair{"abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},std::pair{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}})CHECK(sha256(std::as_bytes(std::span(text,std::strlen(text))))==expected);
}
}
TEST_SUITE("render.palette") {
TEST_CASE("brightness and contrast follow the original HSV value transform") {
 // Reference: HL.EXE 0x320a0 RGB->HSV, 0x32f90 value transform, 0x322b4 HSV->RGB.
 auto reference=[](std::array<uint8_t,3> c,double bright,double contrast){
  double r=c[0]/255.0,g=c[1]/255.0,b=c[2]/255.0,max=std::max({r,g,b}),min=std::min({r,g,b}),v=max,s=max?(max-min)/max:0,h=-1;
  if(s){double d=max-min,rc=(max-r)/d,gc=(max-g)/d,bc=(max-b)/d;h=r==max?bc-gc:g==max?2+rc-bc:4+gc-rc;h*=60;if(h<0)h+=360;}
  v=std::clamp(((v-0.5)*contrast/50+0.5)*bright/50,0.0,1.0);std::array<double,3> out{v,v,v};
  if(s){if(h==360)h=0;h/=60;int i=int(h);double f=h-i,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));switch(i){case 0:out={v,t,p};break;case 1:out={q,v,p};break;case 2:out={p,v,t};break;case 3:out={p,q,v};break;case 4:out={t,p,v};break;default:out={v,p,q};}}
  return std::array<int,3>{int(std::lround(out[0]*255)),int(std::lround(out[1]*255)),int(std::lround(out[2]*255))};};
 std::vector<std::array<uint8_t,3>> colours{{0,0,0},{255,255,255},{128,128,128},{252,128,0},{10,200,40},{60,60,200},{255,0,0},{3,2,1},{200,180,170}};
 for(auto [bright,contrast]:std::initializer_list<std::pair<int,int>>{{50,50},{80,50},{20,50},{50,90},{50,10},{70,30},{100,100},{0,50}}){
  for(auto c:colours){std::vector<uint8_t> px{c[0],c[1],c[2],255};hl::adjust_picture(px,bright/50.0f,contrast/50.0f);auto want=reference(c,bright,contrast);
   INFO(bright<<"/"<<contrast<<" colour "<<int(c[0])<<","<<int(c[1])<<","<<int(c[2]));for(int k=0;k<3;++k)CHECK(std::abs(int(px[k])-want[k])<=1);CHECK(px[3]==255);}
 }
 hl::PaletteView view;for(auto& c:view.source)c={252,128,0};for(size_t i=0;i<256;++i)for(size_t k=0;k<3;++k)view.current[i][k]=view.source[i][k]/4;view.brightness=75;view.contrast=25;
 auto effect=hl::palette_postprocess(view);REQUIRE(effect.has_value());CHECK(effect.value().brightness==1.5f);CHECK(effect.value().contrast==0.5f);CHECK(effect.value().adjusted());
 view.brightness=view.contrast=50;CHECK_FALSE(hl::palette_postprocess(view).value().adjusted());
}
}
