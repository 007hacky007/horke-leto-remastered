#include <doctest/doctest.h>
#include "audio/cd_player.h"
#include "import.h"
using namespace hl;
TEST_SUITE("audio.cd_player") {
TEST_CASE("CD streaming preserves INDEX01 samples seeking looping and stop") {
 auto root=std::filesystem::temp_directory_path()/"hl-cd-player";std::filesystem::create_directories(root);
 std::vector<int16_t> pcm={16384,-16384,8192,-8192,0,0};REQUIRE(encode_flac(pcm,44100,2,root/"track2.flac").has_value());
 {
 CdPlayer cd;REQUIRE(cd.play(2,root/"track2.flac",false).has_value());CHECK(cd.track()==2);CHECK(cd.position()==0);CHECK(cd.buffer_capacity_frames()<=4096);
 std::vector<float> out(4);REQUIRE(cd.mix(out,44100).has_value());CHECK(out==std::vector<float>{.5f,-.5f,.25f,-.25f});CHECK(cd.position()==2);CHECK(cd.playing());
 REQUIRE(cd.seek(0).has_value());out.resize(8);REQUIRE(cd.mix(out,44100).has_value());CHECK_FALSE(cd.playing());CHECK(cd.position()==3);CHECK(out[6]==0);
 REQUIRE(cd.play(2,root/"track2.flac",true).has_value());REQUIRE(cd.mix(out,44100).has_value());CHECK(cd.playing());CHECK(cd.position()==1);CHECK(out[6]==.5f);cd.stop();CHECK_FALSE(cd.playing());REQUIRE(cd.resume().has_value());CHECK(cd.playing());std::vector<float> resumed(2);REQUIRE(cd.mix(resumed,44100).has_value());CHECK(resumed[0]==.25f);cd.stop();
 CHECK_FALSE(cd.play(1,root/"track2.flac",false).has_value());CHECK_FALSE(cd.play(3,root/"track3-pregap.flac",false).has_value());CHECK_FALSE(cd.seek(4).has_value());
 }
 std::filesystem::remove_all(root);
}
}

TEST_SUITE("audio.cd_player") {
TEST_CASE("CD looping retains the resampling remainder across INDEX01 wraps") {
 auto root=std::filesystem::temp_directory_path()/"hl-cd-loop-rate";std::filesystem::create_directories(root);
 std::vector<int16_t> pcm={16384,-16384,8192,-8192,0,0};REQUIRE(encode_flac(pcm,44100,2,root/"track2.flac").has_value());
 {
 CdPlayer cd;REQUIRE(cd.play(2,root/"track2.flac",true).has_value());std::vector<float> out(16);REQUIRE(cd.mix(out,48000).has_value());
 CHECK(out[8]==.5f);CHECK(out[10]==.25f);CHECK(out[12]==0);CHECK(out[14]==.5f);
 }
 std::filesystem::remove_all(root);
}
}
