#include <doctest/doctest.h>
#include "data/character.h"
using namespace hl;
TEST_SUITE("world.scene") {
TEST_CASE("DOS regions keep the anchor component and reject contour vertices") {
 Region r{"component",5,5,1};auto word=[&](int v){r.shape.push_back(std::byte(v&255));r.shape.push_back(std::byte((v>>8)&255));};word(4);for(auto p: {CharacterOffset{10,10},{30,10},{30,30},{10,30}}){word(p.x);word(p.y);word(0);}
 CHECK(r.contains(5,5));CHECK(r.contains(0,0));CHECK_FALSE(r.contains(20,20));CHECK_FALSE(r.contains(10,10));
 r.x=20;r.y=20;CHECK(r.contains(20,20));CHECK_FALSE(r.contains(0,0));CHECK_FALSE(r.contains(30,30));
}
}
