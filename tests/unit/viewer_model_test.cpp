#include <doctest/doctest.h>
#include "viewer.h"
#include "data/flc.h"
#include "../fixtures/flc.h"
#include <set>
TEST_SUITE("assets.viewer") {
TEST_CASE("viewer_visits_every_frame_once") {auto r=hl::ViewerModel::create({{"A.FLC",3},{"B.FLC",2}});REQUIRE(r.has_value());std::set<std::pair<std::string,uint32_t>> seen;for(int i=0;i<5;++i){CHECK(seen.insert({r.value().current().name,r.value().frame()}).second);r.value().advance();}CHECK(seen==std::set<std::pair<std::string,uint32_t>>{{"A.FLC",0},{"A.FLC",1},{"A.FLC",2},{"B.FLC",0},{"B.FLC",1}});CHECK(r.value().frame()==0);CHECK(r.value().current().name=="A.FLC");CHECK_FALSE(r.value().seek("absent",0).has_value());CHECK_FALSE(r.value().seek("a.flc",3).has_value());}
TEST_CASE("viewer_seek_equals_sequential_decode") {auto b=fixture::flc({fixture::frame({fixture::chunk(16,fixture::raw({1,2,3,4}))}),fixture::frame({fixture::chunk(16,fixture::raw({5,6,7,8}))})},2);auto d=hl::FlcDecoder::open(b);REQUIRE(d.has_value());d.value().next();auto sequential=d.value().next().value().value();auto model=hl::ViewerModel::create({{"A.FLC",2}}).value();REQUIRE(model.seek("a.flc",1).has_value());CHECK(d.value().seek(model.frame()).value().indices==sequential.indices);model.previous();CHECK(model.frame()==0);model.previous();CHECK(model.frame()==1);}
}
TEST_SUITE("assets.viewer") {
TEST_CASE("viewer_rejects_nonlogical_canvas_before_render") {CHECK(hl::validate_viewer_canvas(640,480,"ok.flc").has_value());CHECK_FALSE(hl::validate_viewer_canvas(641,480,"wide.flc").has_value());CHECK_FALSE(hl::validate_viewer_canvas(639,480,"narrow.flc").has_value());CHECK_FALSE(hl::validate_viewer_canvas(640,481,"tall.flc").has_value());}
}
