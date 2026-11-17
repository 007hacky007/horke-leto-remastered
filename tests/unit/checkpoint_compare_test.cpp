#include <doctest/doctest.h>
#include "debug/checkpoint.h"
using namespace hl;
TEST_SUITE("debug.checkpoint") {
TEST_CASE("comparison normalizes inventory order but preserves dialogue and scene") {
 nlohmann::json ref={{"schema_version",1},{"scene","intro"},{"inventory",{"b","a"}},{"active_dialogue","Ahoj"},{"variables",nullptr}};
 auto native=ref;native["inventory"]={"a","b"};native["variables"]={{"hidden",3}};
 auto r=compare_checkpoint(ref,native);REQUIRE(r.has_value());CHECK(r.value()["match"]==true);CHECK(r.value()["excluded"]==nlohmann::json::array({"variables","animation_positions","audio_positions"}));
 native["active_dialogue"]="Jine";CHECK(compare_checkpoint(ref,native).value()["match"]==false);
 native=ref;native["scene"]="other";CHECK(compare_checkpoint(ref,native).value()["match"]==false);
}
TEST_CASE("comparison refuses absent reference observables or missing native fields") {
 nlohmann::json ref={{"schema_version",1},{"scene","intro"},{"audio_positions",nullptr}};
 auto native=ref;native.erase("scene");CHECK_FALSE(compare_checkpoint(ref,native).has_value());
 ref.erase("scene");CHECK_FALSE(compare_checkpoint(ref,native).has_value());
 ref["scene"]=nullptr;CHECK_FALSE(compare_checkpoint(ref,native).has_value());
}
}
TEST_SUITE("debug.checkpoint") {
TEST_CASE("all unavailable reference observables are explicitly excluded") {
 nlohmann::json reference={{"schema_version",1},{"scene","intro"}};auto result=compare_checkpoint(reference,reference);REQUIRE(result.has_value());CHECK(result.value()["excluded"].size()==5);
}
}
