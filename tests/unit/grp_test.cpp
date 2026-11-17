#include <doctest/doctest.h>
#include "data/grp.h"
#include "../fixtures/archive.h"
TEST_SUITE("data.grp") {
TEST_CASE("grp_v1_capacity_not_used_count"){auto b=fixture::grp();auto r=hl::GrpArchive::parse(b);REQUIRE(r.has_value());CHECK(r.value().capacity()==2);CHECK(r.value().entries().size()==1);CHECK(r.value().entries()[0].offset==104);}
TEST_CASE("grp_case_insensitive_lookup_preserves_spelling"){auto r=hl::GrpArchive::parse(fixture::grp());REQUIRE(r.has_value());auto b=r.value().read("mixed.flc");REQUIRE(b.has_value());CHECK(b.value().size()==3);CHECK(b.value()[2]==std::byte{'c'});CHECK(r.value().entries()[0].name=="MiXeD.FLC");}
TEST_CASE("grp_duplicate_names_rejected"){auto b=fixture::grp();fixture::u32(b,28,2);for(size_t i=0;i<30;++i)b[74+i]=b[44+i];b[74]=std::byte{'m'};CHECK_FALSE(hl::GrpArchive::parse(b).has_value());}
TEST_CASE("grp_truncated_directory_rejected"){auto b=fixture::grp();b.resize(90);CHECK_FALSE(hl::GrpArchive::parse(b).has_value());}
TEST_CASE("grp_payload_overflow_rejected"){auto b=fixture::grp();fixture::u32(b,58,0xfffffff0);fixture::u32(b,62,64);CHECK_FALSE(hl::GrpArchive::parse(b).has_value());fixture::u32(b,58,44);CHECK_FALSE(hl::GrpArchive::parse(b).has_value());}
TEST_CASE("grp_rejects_v2_as_v1"){auto b=fixture::grp();fixture::u32(b,24,2);CHECK_FALSE(hl::GrpArchive::parse(b).has_value());}
}
