#include <doctest/doctest.h>
#include "render/viewport.h"
using namespace hl;
TEST_SUITE("render.viewport") {
TEST_CASE("fitted window rejects bars and maps resize fullscreen and high DPI") {
 CHECK_FALSE(Viewport::window_to_logical({0,400},{1280,800,1280,800}));
 auto center=Viewport::window_to_logical({640,400},{1280,800,1280,800});REQUIRE(center);CHECK(center->x==320);CHECK(center->y==240);
 auto dpi=Viewport::window_to_logical({400,300},{800,600,1600,1200});REQUIRE(dpi);CHECK(dpi->x==320);CHECK(dpi->y==240);
 CHECK_FALSE(Viewport::window_to_logical({640,480},{640,480,640,480}));
 CHECK_FALSE(Viewport::window_to_logical({0,0},{0,480,0,480}));
 auto letter=Viewport::window_to_logical({400,100},{800,800,1600,1600});REQUIRE(letter);CHECK(letter->y==0);
 CHECK_FALSE(Viewport::window_to_logical({400,99},{800,800,1600,1600}));
}
}
TEST_SUITE("render.viewport") {
TEST_CASE("default fill uses the full Retina height at fractional enlargement") {
 auto v=Viewport::fit(3024,1964);CHECK(v.height==1964);CHECK(v.width==doctest::Approx(2618.6666667));CHECK(v.x==doctest::Approx(202.6666667));CHECK(v.y==0);CHECK(v.fractional);
 auto full=Viewport::fit(4000,3000);CHECK(full.width==4000);CHECK(full.height==3000);
}
}
TEST_SUITE("render.viewport") {
TEST_CASE("whole canvas multiples and stretch map Retina points through the selected viewport") {
 auto v=Viewport::fit(3024,1964,ScalingMode::Whole);CHECK(v.x==552);CHECK(v.y==262);CHECK(v.width==1920);CHECK(v.height==1440);CHECK_FALSE(v.fractional);
 auto small=Viewport::fit(800,600,ScalingMode::Whole);CHECK(small.width==800);CHECK(small.height==600);CHECK(small.fractional);
 auto stretch=Viewport::fit(3024,1964,ScalingMode::Stretch);CHECK(stretch.x==0);CHECK(stretch.y==0);CHECK(stretch.width==3024);CHECK(stretch.height==1964);
 auto p=Viewport::window_to_logical({276,131},{1512,982,3024,1964},ScalingMode::Whole);REQUIRE(p);CHECK(p->x==0);CHECK(p->y==0);
 CHECK_FALSE(Viewport::window_to_logical({275,131},{1512,982,3024,1964},ScalingMode::Whole));
 auto s=Viewport::window_to_logical({0,0},{1512,982,3024,1964},ScalingMode::Stretch);REQUIRE(s);CHECK(s->x==0);CHECK(s->y==0);
 for(auto mode:{ScalingMode::Fill,ScalingMode::Whole,ScalingMode::Stretch}){
  auto c=Viewport::window_to_logical({756,491},{1512,982,3024,1964},mode);REQUIRE(c);CHECK(c->x==320);CHECK(c->y==240);
  CHECK_FALSE(Viewport::window_to_logical({1512,982},{1512,982,3024,1964},mode));
 }
}
}
