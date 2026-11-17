#include <doctest/doctest.h>
#include "debug/checkpoint.h"
using namespace hl;
TEST_SUITE("debug.checkpoint") {
TEST_CASE("checkpoint_canonical_order") {
 Checkpoint a{10,"intro"},b{10,"intro"};
 a.variables={{"z",2},{"a",1}}; b.variables={{"a",1},{"z",2}};
 CHECK(canonical_checkpoint(a)==canonical_checkpoint(b));
 auto j=nlohmann::json::parse(canonical_checkpoint(a)); CHECK(j["inventory"].is_null()); CHECK(j["audio_positions"].is_null()); CHECK(j["schema_version"]==1);
}
}
