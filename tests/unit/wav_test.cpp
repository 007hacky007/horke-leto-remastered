#include <doctest/doctest.h>
#include "data/wav.h"
#include "../fixtures/archive.h"
namespace {std::vector<std::byte> wav(bool odd=false){std::vector<std::byte>b(48+(odd?10:0));fixture::text(b,0,"RIFF");fixture::u32(b,4,b.size()-8);fixture::text(b,8,"WAVEfmt ");fixture::u32(b,16,16);fixture::u16(b,20,1);fixture::u16(b,22,1);fixture::u32(b,24,22050);fixture::u32(b,28,44100);fixture::u16(b,32,2);fixture::u16(b,34,16);size_t p=36;if(odd){fixture::text(b,p,"JUNK");fixture::u32(b,p+4,1);b[p+8]=std::byte{1};p+=10;}fixture::text(b,p,"data");fixture::u32(b,p+4,4);fixture::u16(b,p+8,0x8000);fixture::u16(b,p+10,0x7fff);return b;}}
TEST_SUITE("data.wav") {
TEST_CASE("wav_odd_chunk_padding"){auto r=hl::decode_wav(wav(true));REQUIRE(r.has_value());CHECK(r.value().samples==std::vector<int16_t>{-32768,32767});}
TEST_CASE("wav_unknown_chunks"){auto b=wav(true);fixture::text(b,36,"ABCD");auto r=hl::decode_wav(b);REQUIRE(r.has_value());CHECK(r.value().rate==22050);CHECK(r.value().channels==1);CHECK(r.value().frames==2);}
TEST_CASE("wav_truncated_data"){auto b=wav();b.pop_back();CHECK_FALSE(hl::decode_wav(b).has_value());b=wav();fixture::u32(b,40,0xfffffff0);CHECK_FALSE(hl::decode_wav(b).has_value());}
TEST_CASE("wav_format_alignment"){for(auto [offset,value]:{std::pair{20,3},std::pair{32,4},std::pair{34,24},std::pair{22,0}}){auto b=wav();fixture::u16(b,offset,value);CHECK_FALSE(hl::decode_wav(b).has_value());}}
}
