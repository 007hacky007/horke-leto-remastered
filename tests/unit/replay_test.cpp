#include <doctest/doctest.h>
#include "debug/replay.h"
#include <sstream>
#include <nlohmann/json.hpp>
using namespace hl;
TEST_SUITE("debug.replay") {
TEST_CASE("replay_keeps_equal_tick_order") {
 std::istringstream in(R"({"schema_version":1,"tick":10,"kind":"key_down","key":"enter"}
{"schema_version":1,"tick":10,"kind":"key_up","key":"enter"}
{"schema_version":1,"tick":20,"kind":"mouse_down","logical_x":10,"logical_y":20,"button":"left"}
)");
 auto r=read_trace(in); REQUIRE(r.has_value()); Replay replay(r.value());
 CHECK(replay.events_at(9).empty()); auto events=replay.events_at(10);
 REQUIRE(events.size()==2); CHECK(events[0].kind=="key_down"); CHECK(events[1].kind=="key_up");
 CHECK(replay.events_at(10).empty()); CHECK(replay.events_at(25).size()==1);
}
TEST_CASE("replay_rejects_backward_ticks") {
 std::istringstream in(R"({"schema_version":1,"tick":2,"kind":"key_down","key":"a"}
{"schema_version":1,"tick":1,"kind":"key_up","key":"a"})");
 CHECK_FALSE(read_trace(in).has_value());
}
TEST_CASE("replay_preserves_click_and_key_release") {
 std::istringstream in(R"({"schema_version":1,"tick":0,"kind":"mouse_down","logical_x":639,"logical_y":479,"button":"right"}
{"schema_version":1,"tick":1,"kind":"mouse_up","logical_x":639,"logical_y":479,"button":"right"}
{"schema_version":1,"tick":2,"kind":"key_down","key":"enter"}
{"schema_version":1,"tick":3,"kind":"key_up","key":"enter"})");
 auto r=read_trace(in); REQUIRE(r.has_value()); CHECK(r.value().size()==4); CHECK(r.value()[1].kind=="mouse_up"); CHECK(r.value()[3].kind=="key_up");
}
TEST_CASE("malformed_trace_reports_line") {
 for(auto s: {R"({"schema_version":1,"tick":-1,"kind":"key_down","key":"a"})",R"({"schema_version":1,"tick":1,"kind":"wat"})",R"({"schema_version":1,"tick":1,"kind":"mouse_down","logical_x":640,"logical_y":0,"button":"left"})","{"}) { std::istringstream in(s); auto r=read_trace(in); REQUIRE_FALSE(r.has_value()); CHECK(r.error().byte_offset==1); }
}
}

TEST_SUITE("debug.replay") {
TEST_CASE("native save editor replay accepts bounded text input") {
 std::istringstream in(R"({"schema_version":1,"tick":10,"kind":"text_input","text":"Chapter 1"})");
 auto r=read_trace(in);REQUIRE(r.has_value());CHECK(r.value()[0].kind=="text_input");
 for(auto text:{std::string{},std::string(4097,'x')}){nlohmann::json event{{"schema_version",1},{"tick",10},{"kind","text_input"},{"text",text}};std::istringstream bad(event.dump());CHECK_FALSE(read_trace(bad).has_value());}
}
}
