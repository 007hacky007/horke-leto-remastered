#include <doctest/doctest.h>
#include "coverage.h"
namespace {hl::CoverageState valid(){hl::CoverageState s;s.expected={{"A.FLC",2}};s.decoded={{"A.FLC",{0,1}}};s.exported=s.decoded;s.wav_expected={"a.wav"};s.wav_decoded=s.wav_expected;s.audio_tracks={2,3};s.text_font_valid=true;s.transparency_valid=true;s.exception_names={"IN-PR-MO.FLC","IN-PRE-M.FLC","MAPA7-BL.FLC"};return s;}}
TEST_SUITE("assets.coverage") {
TEST_CASE("coverage_missing_frame_fails") {auto s=valid();REQUIRE(hl::validate_coverage(s).has_value());s.exported["A.FLC"].erase(1);CHECK_FALSE(hl::validate_coverage(s).has_value());}
TEST_CASE("coverage_audio_decode_error_fails") {auto s=valid();s.wav_errors=1;CHECK_FALSE(hl::validate_coverage(s).has_value());s=valid();s.wav_decoded.clear();CHECK_FALSE(hl::validate_coverage(s).has_value());}
TEST_CASE("coverage_depth_zero_requires_evidence") {auto s=valid();s.exception_names.erase("IN-PRE-M.FLC");CHECK_FALSE(hl::validate_coverage(s).has_value());}
}
#include "assets.h"
#include "data/hash.h"
namespace {
struct ExportFixture {hl::IndexedFrame frame;hl::TransparencyRule rule{0,"fixture"};hl::CroppedFrame crop;std::vector<uint8_t> rgba{33,44,55,0,0,0,0,255,77,88,99,255,33,44,55,0};nlohmann::json metadata,usage{{"flc_type_0_overlay","index 0 transparent"},{"first_full_screen_base","opaque copy of complete restored indices; palette index 0 remains opaque"}};
 ExportFixture(){frame.logical_width=2;frame.logical_height=2;frame.indices={0,1,2,0};frame.palette[0]={33,44,55};frame.palette[1]={0,0,0};frame.palette[2]={77,88,99};frame.declared_delay={71,1000};frame.effective_delay={3,1,hl::DelayUnit::engine_updates};crop=hl::crop_frame(frame,rule).value();metadata=hl::frame_metadata(frame,rule);metadata["crop"]={{"x",0},{"y",0},{"width",2},{"height",2},{"empty",false}};metadata["indices_sha256"]=hl::sha256(std::as_bytes(std::span(frame.indices)));}
 auto check(){return hl::validate_frame_export(frame,metadata,usage,crop,rgba,rule);}
};
}
TEST_SUITE("assets.coverage") {
TEST_CASE("coverage_modified_palette_fails") {ExportFixture f;REQUIRE(f.check().has_value());f.metadata["palette"][1][0]=1;CHECK_FALSE(f.check().has_value());}
TEST_CASE("coverage_modified_timing_fails") {ExportFixture f;f.metadata["effective_delay"]["numerator"]=4;CHECK_FALSE(f.check().has_value());}
TEST_CASE("coverage_missing_usage_rules_fails") {ExportFixture f;f.usage.erase("first_full_screen_base");CHECK_FALSE(f.check().has_value());}
TEST_CASE("coverage_rgba_and_mask_follow_indices") {ExportFixture f;f.crop.mask[1]=0;CHECK_FALSE(f.check().has_value());f=ExportFixture{};f.rgba[7]=0;CHECK_FALSE(f.check().has_value());}
TEST_CASE("coverage_transparency_requires_opaque_base_evidence") {nlohmann::json j{{"overlay_index",0},{"opaque_base_rule","first fullscreen copy"},{"opaque_base_usage",{{"condition","first_full_screen_base"},{"mode","opaque_copy"}}},{"runtime_probe",{{"match",true}}}};REQUIRE(hl::validate_transparency_evidence(j).has_value());j.erase("opaque_base_rule");CHECK_FALSE(hl::validate_transparency_evidence(j).has_value());}
}
