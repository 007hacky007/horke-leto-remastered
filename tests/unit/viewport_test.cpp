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
