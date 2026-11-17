#include <doctest/doctest.h>
#include "render/palette.h"
#include "data/hash.h"
#include <cstring>
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
