#include <doctest/doctest.h>
#include "assets.h"
namespace {hl::IndexedFrame frame(){hl::IndexedFrame f;f.logical_width=3;f.logical_height=2;f.indices={7,7,7,7,1,7};f.palette[7]={255,0,0};f.palette[1]={0,0,0};f.declared_delay={0,1000};f.effective_delay={1,1,hl::DelayUnit::engine_updates};return f;}}
TEST_SUITE("assets.frames") {
TEST_CASE("opaque_black_stays_opaque"){auto r=hl::crop_frame(frame(),{7,"fixture"});REQUIRE(r.has_value());CHECK(r.value().indices==std::vector<uint8_t>{1});CHECK(r.value().mask==std::vector<uint8_t>{255});}
TEST_CASE("transparent_index_ignores_rgb"){auto r=hl::crop_frame(frame(),{7,"fixture"});REQUIRE(r.has_value());CHECK(r.value().x==1);CHECK(r.value().y==1);CHECK(r.value().width==1);CHECK(r.value().height==1);}
TEST_CASE("empty_frame_has_no_invalid_crop"){auto f=frame();f.indices.assign(6,7);auto r=hl::crop_frame(f,{7,"fixture"});REQUIRE(r.has_value());CHECK(r.value().empty);CHECK(r.value().width==0);CHECK(r.value().height==0);auto canvas=hl::restore_frame(r.value(),3,2,7);REQUIRE(canvas.has_value());CHECK(canvas.value()==f.indices);}
TEST_CASE("crop_roundtrip_restores_canvas"){auto f=frame();f.indices[0]=2;auto r=hl::crop_frame(f,{7,"fixture"});REQUIRE(r.has_value());auto back=hl::restore_frame(r.value(),3,2,7);REQUIRE(back.has_value());CHECK(back.value()==f.indices);auto malformed=r.value();malformed.x=UINT32_MAX;CHECK_FALSE(hl::restore_frame(malformed,3,2,7).has_value());}
TEST_CASE("frame_metadata_retains_palette_and_timing"){auto f=frame();auto j=hl::frame_metadata(f,{7,"fixture"});CHECK(j["palette"][7][0]==255);CHECK(j["declared_delay"]["numerator"]==0);CHECK(j["effective_delay"]["unit"]=="engine_updates");CHECK(j["logical_width"]==3);CHECK(j["logical_height"]==2);}
}
#include "../fixtures/flc.h"
#include "image_io.h"
TEST_SUITE("assets.frames") {
TEST_CASE("png_export_roundtrip_retains_indices_and_mask"){
 auto dir=std::filesystem::temp_directory_path()/"hl-frames-test";std::filesystem::remove_all(dir);auto b=fixture::flc({fixture::frame({fixture::chunk(16,fixture::raw({0,1,2,0}))})});auto d=hl::FlcDecoder::open(b,"fixture.flc");REQUIRE(d.has_value());auto r=hl::export_flc(d.value(),{0,"fixture"},dir/"fixture.flc");REQUIRE(r.has_value());CHECK(r.value()["frames"][0]["roundtrip_equal"]==true);auto image=hl::read_png(dir/"fixture.flc/frame-0.indices.png",1);REQUIRE(image.has_value());CHECK(image.value().pixels==std::vector<uint8_t>{0,1,2,0});auto mask=hl::read_png(dir/"fixture.flc/frame-0.mask.png",1);REQUIRE(mask.has_value());CHECK(mask.value().pixels==std::vector<uint8_t>{0,255,255,0});std::filesystem::remove_all(dir);
}
}
