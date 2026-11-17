#include <doctest/doctest.h>
#include "data/disc.h"
#include <fstream>
#include <limits>
namespace { struct DiscFixture {
 std::filesystem::path dir=std::filesystem::temp_directory_path()/"hl-disc-fixture";
 DiscFixture(){std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);}
 ~DiscFixture(){std::filesystem::remove_all(dir);}
 void write(std::string name,const std::string& bytes){std::ofstream f(dir/name,std::ios::binary);f.write(bytes.data(),bytes.size());}
}; }
TEST_SUITE("data.disc") {
TEST_CASE("cue_mmssff_and_indices") {
 auto r=hl::parse_cue("FILE \"disc.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n TRACK 02 AUDIO\n PREGAP 00:02:00\n INDEX 01 32:48:06\n TRACK 03 AUDIO\n INDEX 00 38:11:25\n INDEX 01 38:13:06\n");
 REQUIRE(r.has_value()); REQUIRE(r.value().tracks.size()==3);
 CHECK(r.value().tracks[1].index01==147606); CHECK(r.value().tracks[1].pregap==150);
 CHECK(r.value().tracks[2].index00==171850); CHECK(r.value().tracks[2].index01==171981);
 CHECK_FALSE(hl::parse_cue("FILE \"d\" BINARY\nTRACK 1 AUDIO\nINDEX 01 00:60:00\n").has_value());
}
TEST_CASE("cue_missing_or_escaping_bin_rejected") {
 CHECK_FALSE(hl::parse_cue("TRACK 01 AUDIO\nINDEX 01 00:00:00\n").has_value());
 CHECK_FALSE(hl::parse_cue("FILE \"../disc.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n").has_value());
 DiscFixture f; f.write("disc.cue","FILE \"absent.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
 CHECK_FALSE(hl::DiscReader::open(f.dir/"disc.cue").has_value());
}
TEST_CASE("mode1_extracts_exact_payload") {
 DiscFixture f; std::string bytes(2352*2,'\0');for(size_t i=0;i<bytes.size();++i)bytes[i]=char(i%251);
 f.write("disc.bin",bytes);f.write("disc.cue","FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
 auto d=hl::DiscReader::open(f.dir/"disc.cue"); REQUIRE(d.has_value()); auto s=d.value().read_mode1_sector(1);REQUIRE(s.has_value());
 for(size_t i=0;i<2048;++i)CHECK(s.value()[i]==std::byte(static_cast<unsigned char>(bytes[2352+16+i])));
 CHECK_FALSE(d.value().read_mode1_sector(2).has_value());CHECK_FALSE(d.value().read_mode1_sector(UINT64_MAX).has_value());
}
TEST_CASE("mode1_partial_sector_rejected") {
 DiscFixture f; f.write("disc.bin",std::string(2353,'\0')); f.write("disc.cue","FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
 CHECK_FALSE(hl::DiscReader::open(f.dir/"disc.cue").has_value());
}
}
TEST_SUITE("data.disc") {
TEST_CASE("overlapping_cue_indices_cannot_wrap_data_extent") {DiscFixture f;f.write("disc.bin",std::string(2352*3,'\0'));f.write("disc.cue","FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:01\nTRACK 02 AUDIO\nINDEX 00 00:00:00\nINDEX 01 00:00:02\n");CHECK_FALSE(hl::DiscReader::open(f.dir/"disc.cue").has_value());}
}
