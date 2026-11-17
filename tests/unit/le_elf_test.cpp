#include <doctest/doctest.h>
#include "le/le_to_elf.h"
#include "../fixtures/le/synthetic.h"
using namespace hl;
TEST_SUITE("le.elf") {
TEST_CASE("elf_section_bases_and_true_entry") {
 auto m=parse_le(fixture::fixup());REQUIRE(m.has_value());auto r=apply_fixups(m.value());REQUIRE(r.has_value());auto e=make_elf(r.value());REQUIRE(e.has_value());auto& b=e.value();
 auto u=[&](unsigned p){unsigned v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<unsigned>(b.at(p+i))<<(8*i);return v;};
 CHECK(b[0]==std::byte{0x7f});CHECK(u(24)==0x10007);CHECK(u(52+8)==0x10000);CHECK(u(84+8)==0x20000);CHECK(u(32)>0);
}
}
