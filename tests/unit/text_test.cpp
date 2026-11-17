#include <doctest/doctest.h>
#include "data/text.h"
#include "../fixtures/flc.h"
TEST_SUITE("data.text") {
TEST_CASE("czech_known_strings_decode"){
 auto raw=fixture::raw({0x4b,0x20,0x74,0xa0,0x62,0x6f,0xa9,0x69,0xa8,0x74,0x69,0});CHECK(hl::decode_game_text(raw,hl::GameEncoding::kamenicky).value()=="K tábořišti");CHECK(hl::decode_game_text(fixture::raw({0x44,0x6f,0x20,0x64,0x91,0x75,0x6e,0x67,0x6c,0x65,0}),hl::GameEncoding::kamenicky).value()=="Do džungle");auto encrypted=fixture::raw({0xe2,0xa6,0xfd,0x30,0xf7,0xff,0x20,0xef,0x77,0xff,0xef,0x92});CHECK(hl::decrypt_game_text(encrypted)==raw);
}
TEST_CASE("text_controls_not_printed_as_glyphs"){
 auto r=hl::parse_game_text(fixture::raw({'A',13,10,'B',';', 'C',0}),hl::GameEncoding::kamenicky);REQUIRE(r.has_value());CHECK(r.value().utf8=="A\nB\nC");CHECK(r.value().controls.size()==4);CHECK_FALSE(hl::decode_game_text(fixture::raw({1,0}),hl::GameEncoding::kamenicky).has_value());
}
}
