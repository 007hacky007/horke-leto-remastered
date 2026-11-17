#include <doctest/doctest.h>
#include "render/text_overlay.h"
using namespace hl;
TEST_SUITE("render.scene") {
TEST_CASE("original font rows retain the spacing in the original text within the dialogue box") {
 BitmapFont font;for(auto& g:font.glyphs)g.rows.fill(0);font.glyphs['A'].rows[0]=128;
 std::vector<uint8_t> text={'A','A',10,'A',0};auto o=make_text_overlay(font,text,100,200);REQUIRE(o.has_value());CHECK(o.value().x==96);CHECK(o.value().y==196);CHECK(o.value().width==24);CHECK(o.value().height==40);
 REQUIRE(o.value().glyphs.size()==3);CHECK(o.value().glyphs[0].index==65);CHECK(o.value().glyphs[0].x==100);CHECK(o.value().glyphs[1].x==108);CHECK(o.value().glyphs[2].y==216);
 CHECK(o.value().mask[4*24+4]==255);CHECK(o.value().mask[20*24+4]==255);CHECK(o.value().mask[20*24+8]==0);
}
TEST_CASE("choice filtering preserves original IDs and multiline row mapping") {
 BitmapFont font{};
 std::vector<uint8_t> text={'A',13,10,'B',';',13,10,'C',';',10,'D',0};
 auto all=make_choice_text(text,0);REQUIRE(all.has_value());
 CHECK(all.value().row_choices==std::vector<int>{0,0,1,2});
 auto filtered=make_choice_text(text,5);REQUIRE(filtered.has_value());
 CHECK(filtered.value().row_choices==std::vector<int>{0,0,2});
 CHECK(filtered.value().text==std::vector<uint8_t>{'A',10,'B',10,'D',0});
 auto box=make_text_overlay(font,filtered.value().text,115,312);REQUIRE(box.has_value());
 CHECK(choice_at(filtered.value(),box.value(),115,344)==2);
 CHECK_FALSE(choice_at(filtered.value(),box.value(),114,344).has_value());
 CHECK_FALSE(choice_at(filtered.value(),box.value(),115,360).has_value());
 CHECK_FALSE(make_choice_text(std::vector<uint8_t>{'A',0},2).has_value());
 CHECK_FALSE(make_choice_text(std::vector<uint8_t>(4097,'A'),0).has_value());
 std::vector<uint8_t> too_many;for(int i=0;i<40;++i){too_many.push_back('A');too_many.push_back(10);}too_many.push_back('A');too_many.push_back(0);CHECK_FALSE(make_choice_text(too_many,0).has_value());
}
TEST_CASE("speech suppression is consumed once and directions follow DOS mapping") {
 bool suppress=true;CHECK_FALSE(speech_animation_direction(4,suppress).has_value());CHECK_FALSE(suppress);
 CHECK(speech_animation_direction(4,suppress)==3);
 CHECK(speech_animation_direction(2,suppress)==7);CHECK(speech_animation_direction(3,suppress)==7);
 CHECK(speech_animation_direction(5,suppress)==5);CHECK(speech_animation_direction(6,suppress)==5);
 for(int d:{0,1,7})CHECK_FALSE(speech_animation_direction(d,suppress).has_value());
}
}
