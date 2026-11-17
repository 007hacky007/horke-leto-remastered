#include <doctest/doctest.h>
#include "data/adb.h"
#include "../fixtures/archive.h"
namespace {
std::vector<std::byte> adb(){std::vector<std::byte>b(63);fixture::u32(b,0,666);fixture::u32(b,8,1);fixture::u32(b,12,1);fixture::u32(b,16,31);fixture::text(b,24,"main");fixture::u32(b,56,3);fixture::text(b,60,"abc");return b;}
std::vector<std::byte> object(){std::vector<std::byte>b(40);fixture::u16(b,4,33);b[6]=std::byte{1};fixture::u16(b,8,0xdead);fixture::u16(b,10,12);fixture::u16(b,18,0);fixture::u16(b,20,1);fixture::u16(b,29,4);fixture::u16(b,31,0x80);fixture::text(b,33,"main");return b;}
}
TEST_SUITE("data.adb") {
TEST_CASE("adb_relative_offsets_use_payload_base"){auto r=hl::AdbDatabase::parse(adb());REQUIRE(r.has_value());CHECK(r.value().payload_base()==60);auto b=r.value().read("MAIN");REQUIRE(b.has_value());CHECK(b.value()[0]==std::byte{'a'});}
TEST_CASE("adb_exact_name_boundary"){auto b=adb();fixture::text(b,24,std::string(31,'A'));CHECK(hl::AdbDatabase::parse(b).has_value());b[55]=std::byte{'A'};CHECK_FALSE(hl::AdbDatabase::parse(b).has_value());}
TEST_CASE("adb_truncated_directory"){auto b=adb();b.resize(59);CHECK_FALSE(hl::AdbDatabase::parse(b).has_value());}
TEST_CASE("adb_payload_overflow"){auto b=adb();fixture::u32(b,20,UINT32_MAX);CHECK_FALSE(hl::AdbDatabase::parse(b).has_value());}
}
TEST_SUITE("data.object_layout") {
TEST_CASE("object_header_size_matches"){for(size_t n=0;n<7;++n)CHECK_FALSE(hl::inspect_object_header(std::vector<std::byte>(n)).has_value());auto b=object();CHECK(hl::inspect_object_header(b).has_value());b.pop_back();CHECK_FALSE(hl::inspect_object_header(b).has_value());}
TEST_CASE("resource_names_terminate_in_table"){auto b=object();fixture::u16(b,29,100);CHECK_FALSE(hl::inspect_object_header(b).has_value());b=object();for(size_t i=33;i<b.size();++i)b[i]=std::byte{'a'};CHECK_FALSE(hl::inspect_object_header(b).has_value());}
TEST_CASE("handler_declaration_is_not_code"){auto r=hl::inspect_object_header(object());REQUIRE(r.has_value());CHECK(r.value().table_offset==22);CHECK(r.value().code_end==22);CHECK(r.value().resources.size()==1);CHECK(r.value().resources[0].name=="main");CHECK(r.value().resources[0].flags==0x80);}
}
