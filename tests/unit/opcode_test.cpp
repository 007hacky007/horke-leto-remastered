#include <doctest/doctest.h>
#include "script/opcode.h"
#include <vector>
using namespace hl;
TEST_SUITE("script.opcode") {
TEST_CASE("DOS immediates retain signed and unsigned operand widths") {
 const auto map=dos_opcode_map();
 struct Form {unsigned op,width;int64_t value;};
 for(auto f: {Form{9,4,-1},Form{10,2,-1},Form{11,1,-1},Form{12,2,-1},Form{13,1,-1},Form{15,2,65535},Form{16,1,255}}) {
  std::vector<std::byte> b(1+f.width,std::byte{255}); b[0]=std::byte(f.op);
  auto r=decode_instruction(b,0,map,"signed"); REQUIRE(r.has_value());
  CHECK(r.value().operand==f.value); CHECK(r.value().next_offset==b.size());
 }
}
TEST_CASE("DOS branch displacements use the end of their operand") {
 std::vector<std::byte> b{std::byte{3},std::byte{0xfd},std::byte{0xff}};
 auto r=decode_instruction(b,0,dos_opcode_map(),"loop");REQUIRE(r.has_value());CHECK(r.value().branch_target==0);
 b[0]=std::byte{54};b[1]=std::byte{1};b[2]=std::byte{0};
 r=decode_instruction(b,0,dos_opcode_map(),"handler");REQUIRE(r.has_value());CHECK(r.value().branch_target==4);CHECK(r.value().flow==InstructionFlow::declaration);
}
TEST_CASE("unknown instructions and truncation retain object and opcode offset") {
 for(unsigned op:{1,2,62,76,170,255}) {
  std::vector<std::byte> b{std::byte{7},std::byte(op)};auto r=decode_instruction(b,1,dos_opcode_map(),"named-object");
  REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="named-object");CHECK(r.error().byte_offset==1);CHECK(r.error().reason.find("unknown opcode")!=std::string::npos);
 }
 for(unsigned op:{0,3,4,9,10,11,12,13,15,16,54,55,56,57,58,59,60}) {
  std::vector<std::byte> b{std::byte(op)};auto r=decode_instruction(b,0,dos_opcode_map(),"short");REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="short");CHECK(r.error().byte_offset==0);
 }
}
TEST_CASE("DOS map is explicit and rejects the blanket plus-five interpretation") {
 auto m=dos_opcode_map();CHECK(m[14].handler_address==0x28044);CHECK(m[15].handler_address==0x28058);
 CHECK(m[0].operand_bytes==4);CHECK(m[54].operand_bytes==2);CHECK(m[123].operand_bytes==0);
 CHECK(m[1].handler_address==0);CHECK(m[62].handler_address==0);
}
}
