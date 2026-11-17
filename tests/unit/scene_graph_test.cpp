#include <doctest/doctest.h>
#include "render/scene_graph.h"
#include "data/frame_crop.h"
using namespace hl;
TEST_SUITE("render.scene") {
TEST_CASE("indexed composition uses the DOS transparent edge clip") {
 IndexedFrame base;base.logical_width=640;base.logical_height=480;base.indices.assign(640*480,2);
 IndexedFrame cursor;cursor.logical_width=3;cursor.logical_height=3;cursor.indices.assign(9,7);
 SceneGraph graph;graph.layers={{&base,0,0,480,{}},{&cursor,638,478,-100,0}};
 auto frame=graph.compose();REQUIRE(frame.has_value());CHECK(frame.value()[478*640+638]==7);
 CHECK(frame.value()[478*640+639]==2);CHECK(frame.value()[479*640+638]==2);
}
TEST_CASE("DOS surface bounds omit a final aligned singleton from the left column scan") {
 IndexedFrame f;f.logical_width=8;f.logical_height=2;f.indices.resize(16);f.indices[5]=2;f.indices[12]=1;
 auto c=crop_dos_surface(f);REQUIRE(c.has_value());
 CHECK(c.value().x==5);CHECK(c.value().y==0);CHECK(c.value().width==1);CHECK(c.value().height==2);
 CHECK(c.value().indices==std::vector<uint8_t>{2,0});
 // Exported source pixels remain intact.
 CHECK(crop_frame(f,{0,"source export"}).value().x==4);
 f.indices[13]=3;c=crop_dos_surface(f);REQUIRE(c.has_value());CHECK(c.value().x==4);
}
TEST_CASE("DOS Z ordering preserves opaque zero in the base and transparent overlays") {
 IndexedFrame base;base.logical_width=2;base.logical_height=1;base.indices={0,2};
 IndexedFrame overlay=base;overlay.indices={3,0};SceneGraph g;g.layers={{&overlay,0,0,20,0},{&base,0,0,480,{}}};
 auto r=g.compose();REQUIRE(r.has_value());CHECK(r.value()[0]==3);CHECK(r.value()[1]==2);
 g.layers[0].x=-1;r=g.compose();CHECK(r.value()[0]==0);CHECK(r.value()[1]==2);
}
TEST_CASE("invalid frames stop and large offscreen anchors do not overflow") {
 IndexedFrame f;f.logical_width=1;f.logical_height=1;SceneGraph g;g.layers={{&f,0,0,0,0}};CHECK_FALSE(g.compose().has_value());
 f.indices={9};g.layers[0].x=INT32_MAX;CHECK(g.compose().value()[0]==0);
}
}

TEST_SUITE("render.scene") {
TEST_CASE("CHR frames anchor their nonzero surface at the feet") {
 IndexedFrame f;f.logical_width=8;f.logical_height=8;f.indices.resize(64);f.indices[3*8+2]=1;f.indices[4*8+3]=2;
 auto p=place_character_frame(f,{0,0},20,30,10000);REQUIRE(p.has_value());CHECK(p.value().x==19);CHECK(p.value().y==28);CHECK(p.value().frame.logical_width==2);CHECK(p.value().frame.logical_height==2);CHECK(p.value().frame.indices==std::vector<uint8_t>{1,0,0,2});
}
}
TEST_SUITE("render.scene") {
TEST_CASE("GPU surfaces preserve palette-expanded pixels and transparent overlays") {
 RenderSurface base{0,0,480,2,1,{1,2,3,255,4,5,6,255}};
 RenderSurface overlay{0,0,0,2,1,{7,8,9,255,0,0,0,0}};
 auto rgba=flatten_surfaces({overlay,base});REQUIRE(rgba.has_value());CHECK(rgba.value()[0]==7);CHECK(rgba.value()[4]==4);
}
}
TEST_SUITE("render.scene") {
TEST_CASE("renderer captures retain actual dimensions and reject short pixel storage") {
 RenderCapture small{320,240,std::vector<uint8_t>(320*240*4)};CHECK(validate_capture(small).has_value());small.pixels.pop_back();CHECK_FALSE(validate_capture(small).has_value());RenderCapture large{1280,720,std::vector<uint8_t>(1280*720*4)};CHECK(validate_capture(large).has_value());
}
}

TEST_SUITE("render.scene") {
TEST_CASE("regional composition preserves Z transparency and offscreen black") {
 IndexedFrame base;base.logical_width=4;base.logical_height=2;base.indices={1,2,3,4,5,6,7,8};
 IndexedFrame top;top.logical_width=2;top.logical_height=2;top.indices={0,9,10,0};
 SceneGraph g;g.layers={{&top,1,0,10,0},{&base,0,0,480,{}}};
 auto r=g.compose(1,0,3,2);REQUIRE(r.has_value());CHECK(r.value()==std::vector<uint8_t>{2,9,4,10,7,8});
 r=g.compose(-1,-1,3,3);REQUIRE(r.has_value());CHECK(r.value()==std::vector<uint8_t>{0,0,0,0,1,2,0,5,10});
 r=g.compose(639,479,2,2);REQUIRE(r.has_value());CHECK(r.value()==std::vector<uint8_t>{0,0,0,0});
 CHECK_FALSE(g.compose(0,0,0,1).has_value());CHECK_FALSE(g.compose(0,0,641,480).has_value());
 g.layers.push_back({nullptr,0,0,0,0});CHECK_FALSE(g.compose(0,0,1,1).has_value());
}
}

TEST_SUITE("render.scene") {
TEST_CASE("fractional character projection samples the original scale before output rounding") {
 IndexedFrame f;f.logical_width=51;f.logical_height=146;
 CroppedFrame c;c.width=51;c.height=146;c.indices.resize(51*146);
 for(unsigned y=0;y<146;++y)for(unsigned x=0;x<51;++x)c.indices[y*51+x]=uint8_t((x+3*y)%255+1);
 auto placed=place_character_frame(f,c,{},0,0,6950);REQUIRE(placed.has_value());
 const auto& actual=placed.value().frame;REQUIRE(actual.logical_width==35);REQUIRE(actual.logical_height==101);
 for(unsigned y=0;y<101;++y)for(unsigned x=0;x<35;++x)REQUIRE(actual.indices[y*35+x]==c.indices[(y*10000/6950)*51+x*10000/6950]);
}
TEST_CASE("reused character crops retain projected pixels and reject malformed storage") {
 IndexedFrame f;f.logical_width=8;f.logical_height=8;f.indices.resize(64);f.indices[3*8+2]=1;f.indices[4*8+3]=2;
 auto crop=crop_dos_surface(f);REQUIRE(crop.has_value());
 auto p=place_character_frame(f,crop.value(),{2,4},20,30,5000);REQUIRE(p.has_value());CHECK(p.value().x==21);CHECK(p.value().y==31);CHECK(p.value().frame.indices==std::vector<uint8_t>{1});
 crop.value().indices.clear();CHECK_FALSE(place_character_frame(f,crop.value(),{},20,30,10000).has_value());
}
}
