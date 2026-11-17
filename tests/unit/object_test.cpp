#include <doctest/doctest.h>
#include "script/object.h"
#include "fixtures/script/object_fixture.h"
using namespace hl;
TEST_SUITE("script.object") {
TEST_CASE("code excludes Enter metadata and the separate resource descriptor/table") {
 auto b=script_fixture::object({13,255,7});auto r=load_object(b,"simple");REQUIRE(r.has_value());
 auto& o=r.value();CHECK(o.name=="simple");CHECK(o.instructions.size()==3);CHECK(o.instructions[0].offset==0);CHECK(o.instructions[1].offset==17);CHECK(o.instructions[2].offset==19);
 CHECK(o.layout.table_offset==25);CHECK(o.layout.resources.size()==1);CHECK(o.layout.resources[0].name=="x");
}
TEST_CASE("handler lengths delimit independent code roots") {
 auto b=script_fixture::object({55,3,0,13,255,7,7});auto r=load_object(b,"handlers");REQUIRE(r.has_value());
 REQUIRE(r.value().handlers.size()==1);CHECK(r.value().handlers[0].entry==20);CHECK(r.value().handlers[0].end==23);
 CHECK(r.value().instructions.size()==5);
}
TEST_CASE("malformed branches cannot enter headers, operands or tables") {
 for(auto body:{std::initializer_list<unsigned>{4,0xee,0xff,7}, {4,0xff,0xff,7}, {4,1,0,13,1,7}, {4,1,0,7}}) {
  auto b=script_fixture::object(body);auto r=load_object(b,"bad-branch");REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="bad-branch");
 }
}
TEST_CASE("unknown reachable opcode identifies its object and offset") {
 auto b=script_fixture::object({255,7});auto r=load_object(b,"unknown-object");REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="unknown-object");CHECK(r.error().byte_offset==17);
}
TEST_CASE("truncated and malformed handler records fail") {
 auto b=script_fixture::object({55,100,0,7});auto r=load_object(b,"long-handler");REQUIRE_FALSE(r.has_value());
 b=script_fixture::object({55,0,0,7});r=load_object(b,"empty-handler");REQUIRE_FALSE(r.has_value());
 b.resize(6);r=load_object(b,"short");REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="short");
}
TEST_CASE("resource metadata cannot hide five arbitrary trailing code bytes") {
 for(size_t field:{size_t(0),size_t(1),size_t(3)}) {
  auto b=script_fixture::object({13,255,7});b[7+20+field]=std::byte{255};
  auto r=load_object(b,"bad-descriptor");REQUIRE_FALSE(r.has_value());CHECK(r.error().resource=="bad-descriptor");CHECK(r.error().byte_offset==20);
 }
}

}
