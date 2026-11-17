#include <doctest/doctest.h>
#include "render/viewport.h"
#include "render/palette.h"
#include "render/scene_graph.h"
using namespace hl;
TEST_SUITE("render.3x") {
TEST_CASE("fit uses integer canvas scale when possible and fractional downscale below native") {auto v=Viewport::fit(4000,3000);CHECK(v.x==80);CHECK(v.y==60);CHECK(v.width==3840);CHECK(v.height==2880);auto small=Viewport::fit(1280,800);CHECK(small.width==doctest::Approx(1066.666667));CHECK(small.height==800);CHECK(small.fractional);auto p=Viewport::window_to_logical({80,60},{4000,3000,4000,3000});REQUIRE(p);CHECK(p->x==0);CHECK(p->y==0);CHECK_FALSE(Viewport::window_to_logical({79,60},{4000,3000,4000,3000}));}
TEST_CASE("pack placements use logical anchors and explicit projection rather than atlas dimensions") {FrameAsset a;a.x=30;a.y=60;a.width=90;a.height=120;auto r=place_asset(a,5,7);CHECK(r.x==45);CHECK(r.y==81);CHECK(r.width==90);CHECK(r.height==120);auto s=place_asset(a,100,200,0.5,0.5);CHECK(s.x==315);CHECK(s.y==630);CHECK(s.width==45);CHECK(s.height==60);}
}
TEST_SUITE("render.palette") {
TEST_CASE("postprocess fade and flash follow the 64-step DAC curve") {auto dark=palette_postprocess(0,false);REQUIRE(dark.has_value());CHECK(dark.value().gain==0);auto mid=palette_postprocess(32,false);REQUIRE(mid.has_value());CHECK(mid.value().gain==doctest::Approx(31.0/63));CHECK(mid.value().bias==0);auto white=palette_postprocess(64,true);REQUIRE(white.has_value());CHECK(white.value().gain==0);CHECK(white.value().bias==1);CHECK_FALSE(palette_postprocess(65,false).has_value());}
}
#include "../fixtures/flc.h"
TEST_SUITE("render.3x") {
TEST_CASE("decoded and projected frames retain original resource frame and crop identity") {auto d=FlcDecoder::open(fixture::flc({fixture::frame({fixture::chunk(16,fixture::raw({0,1,0,1}))})}),"SPRITE.FLC");REQUIRE(d.has_value());auto f=d.value().next();REQUIRE(f.has_value());REQUIRE(f.value());CHECK(f.value()->source.resource=="SPRITE.FLC");CHECK(f.value()->source.frame==0);CHECK(f.value()->source.width==2);auto placed=place_character_frame(*f.value(),{},10,20,10000);REQUIRE(placed.has_value());CHECK(placed.value().frame.source.resource=="SPRITE.FLC");CHECK(placed.value().frame.source.x==1);CHECK(placed.value().frame.source.width==1);}
}

TEST_SUITE("render.3x") {
TEST_CASE("transparent DOS sprites omit the final logical edge only when they cross it") {
 RenderSurface s{630,470,0,20,20,{}};CHECK(surface_clip(s)==std::array<int,2>{1917,1437});
 s.width=10;s.height=10;CHECK(surface_clip(s)==std::array<int,2>{1920,1440});
 s.width=20;s.opaque_base=true;CHECK(surface_clip(s)==std::array<int,2>{1920,1440});
}
}
