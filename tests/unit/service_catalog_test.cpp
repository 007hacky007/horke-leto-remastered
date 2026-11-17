#include <doctest/doctest.h>
#include "script/object.h"
#include "fixtures/script/object_fixture.h"
#include "assets.h"
using namespace hl;
TEST_SUITE("script.catalog") {
TEST_CASE("service catalog retains provenance and resolves only adjacent resource literals") {
 auto object=load_object(script_fixture::object({13,0,97,13,0,113,7}),"catalog").value();auto calls=catalog_service_calls(object);REQUIRE(calls.size()==2);CHECK(calls[0]["object"]=="catalog");CHECK(calls[0]["opcode"]==97);CHECK(calls[0]["literal_resource"]=="x");CHECK(calls[1]["opcode"]==113);
 object.instructions[2].opcode=98;object.instructions[1].opcode=16;calls=catalog_service_calls(object);CHECK(calls[0]["literal_resource"].is_null());
}
TEST_CASE("coverage separates corpus instructions from implemented bindings and observed execution") {
 auto object=load_object(script_fixture::object({13,0,163,13,0,13,0,120,13,0,156,7}),"catalog").value();
 auto report=opcode_coverage(object,{{{"object","catalog"},{"offset",19},{"opcode",163}}});
 CHECK(report["opcodes"]["163"]["runtime_bound"]==true);
 CHECK(report["opcodes"]["120"]["runtime_bound"]==true);
 CHECK(report["opcodes"]["156"]["runtime_bound"]==true);
 CHECK(report["opcodes"]["163"]["executed_offsets"]==nlohmann::json::array({19}));
 CHECK(report["opcodes"]["120"]["executed_offsets"].empty());
 CHECK(report["opcodes"]["156"]["offsets"].size()==1);
}
TEST_CASE("handler coverage records reached bodies separately from declarations") {
 auto object=load_object(script_fixture::object({55,3,0,13,0,7,7}),"handler").value();
 auto missing=opcode_coverage(object);REQUIRE(missing["handlers"].size()==1);CHECK(missing["handlers"][0]["entry_reached"]==false);
 auto reached=opcode_coverage(object,{{{"object","handler"},{"offset",20},{"opcode",13}}});CHECK(reached["handlers"][0]["entry_reached"]==true);CHECK(reached["initialization_reached"]==false);
}
}
