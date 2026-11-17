#include <doctest/doctest.h>
#include "data/disc.h"
#include "data/flac.h"
#include "import.h"
#include <fstream>
namespace {hl::DiscLayout layout(){return {"",{{1,true,0,{},0},{2,false,147606,{},150},{3,false,171981,171850,0}},179181};}}
TEST_SUITE("data.cd") {
TEST_CASE("cd_index01_starts_track"){auto r=hl::cd_track_ranges(layout());REQUIRE(r.has_value());CHECK(r.value()[0].begin==147606);CHECK(r.value()[1].begin==171981);}
TEST_CASE("cd_track3_index00_not_track2_tail"){auto r=hl::cd_track_ranges(layout()).value();CHECK(r[0].end==171850);CHECK(r[2].begin==171850);CHECK(r[2].end==171981);CHECK(r[2].pregap);}
TEST_CASE("cd_pregap_not_double_counted"){auto d=layout();auto a=hl::cd_track_ranges(d).value();d.tracks[1].pregap=0;auto b=hl::cd_track_ranges(d).value();CHECK(a[0].begin==b[0].begin);CHECK(a[0].end-a[0].begin==24244);CHECK(a[1].end-a[1].begin==7200);}
TEST_CASE("cd_truncated_sector_rejected"){auto path=std::filesystem::temp_directory_path()/"hl-cd-partial.bin";std::ofstream f(path,std::ios::binary);f<<std::string(2353,'\0');f.close();auto d=layout();d.bin=path;CHECK_FALSE(hl::extract_cd_pcm(d,{2,0,1,false}).has_value());std::filesystem::remove(path);}
TEST_CASE("flac_roundtrip_exact_pcm"){
 auto root=std::filesystem::temp_directory_path()/"hl-flac-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root);std::vector<int16_t> samples{-32768,-1,0,1,32767,16384,-300,99,0,1};auto r=hl::encode_flac(samples,44100,2,root/"samples.flac");REQUIRE(r.has_value());auto decoded=hl::decode_flac(root/"samples.flac");REQUIRE(decoded.has_value());CHECK(decoded.value().samples==samples);CHECK(decoded.value().rate==44100);CHECK(decoded.value().channels==2);CHECK(decoded.value().frames==5);std::filesystem::remove_all(root);
}
}
