#include <doctest/doctest.h>
#include "script/intro_movement.h"
using namespace hl;
TEST_SUITE("script.intro_movement") {
TEST_CASE("intro arrival follows the strict DOS distance boundary") {
 Region area;area.x=15;area.y=15;area.flags=1;area.shape.resize(26);area.shape[0]=std::byte(4);auto word=[&](size_t p,unsigned v){area.shape[p]=std::byte(v&255);area.shape[p+1]=std::byte(v>>8);};
 word(2,0);word(4,0);word(8,30);word(10,0);word(14,30);word(16,30);word(20,0);word(22,30);
 auto r=IntroWalk::begin(area,1,1,21,1,5,480,500);REQUIRE(r.has_value());auto& w=r.value();
 for(unsigned i=1;i<=4;++i){w.advance();CHECK(w.x()==int(i*5+1));CHECK_FALSE(w.arrived());}
 w.advance();CHECK(w.arrived());CHECK(w.x()==21);CHECK(w.y()==1);
 CHECK_FALSE(IntroWalk::begin(area,0,0,40,0,5,480,500).has_value());
 CHECK_FALSE(IntroWalk::begin(area,0,0,20,20,5,480,500).has_value());
}
TEST_CASE("DOS depth projection reaches zero and full size at the original planes") {
 CHECK(character_scale(14,240,500)==1307);CHECK(character_scale(240,240,500)==10000);CHECK(character_scale(-20,240,500)==0);CHECK(character_scale(300,240,500)==10000);
}
}
TEST_SUITE("script.intro_movement") {
TEST_CASE("fractional movement rounds at half before carrying its signed remainder") {
 Region area;area.x=15;area.y=15;area.flags=1;area.shape.resize(26);area.shape[0]=std::byte(4);auto word=[&](size_t p,unsigned v){area.shape[p]=std::byte(v&255);area.shape[p+1]=std::byte(v>>8);};
 word(8,30);word(14,30);word(16,480);word(22,480);
 auto w=IntroWalk::begin(area,1,450,21,450,5,0,60).value();w.advance();CHECK(w.x()==4);w.advance();CHECK(w.x()==6);
}

}
