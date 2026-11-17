#include <doctest/doctest.h>
#include "audio/mixer.h"
using namespace hl;
TEST_SUITE("audio.mixer") {
TEST_CASE("DOS voice priority drops lower priority requests and evicts the first worst channel") {
 Mixer m(4,2);PcmAudio pcm;pcm.rate=4;pcm.channels=1;pcm.frames=4;pcm.samples={16384,16384,16384,16384};
 auto first=m.play(pcm,1,0,1,200).value(),second=m.play(pcm,1,0,1,110).value();
 auto dropped=m.play(pcm,1,0,1,201);REQUIRE(dropped.has_value());CHECK(dropped.value()==0);CHECK_FALSE(m.finished(first));CHECK_FALSE(m.finished(second));
 auto replacement=m.play(pcm,1,0,1,100);REQUIRE(replacement.has_value());CHECK(m.finished(first));CHECK_FALSE(m.finished(second));
 auto equal=m.play(pcm,1,0,1,110);REQUIRE(equal.has_value());CHECK(m.finished(second));CHECK_FALSE(m.finished(replacement.value()));
 m.stop_all();CHECK(m.play(pcm,1,0,1,200).value()!=0);
}
TEST_CASE("mixing converts mono rate pan and signals completion at the last sample") {
 Mixer m(4);PcmAudio pcm;pcm.rate=2;pcm.channels=1;pcm.frames=2;pcm.samples={16384,-16384};
 auto id=m.play(pcm,1,0);REQUIRE(id.has_value());std::vector<float> out(6);m.mix(out);CHECK(out==std::vector<float>{.5f,.5f,.5f,.5f,-.5f,-.5f});CHECK_FALSE(m.finished(id.value()));
 out.resize(2);m.mix(out);CHECK(m.finished(id.value()));CHECK(m.position(id.value())==2);
 auto left=m.play(pcm,1,-1).value();out.resize(4);m.mix(out);CHECK(out[0]==.5f);CHECK(out[1]==0);m.stop(left);CHECK(m.finished(left));
}
TEST_CASE("summed voices clip and invalid buffers do not corrupt playback") {
 Mixer m(2);PcmAudio pcm;pcm.rate=2;pcm.channels=2;pcm.frames=1;pcm.samples={32767,-32768};
 REQUIRE(m.play(pcm,1,0).has_value());REQUIRE(m.play(pcm,1,0).has_value());std::vector<float> out(2);m.mix(out);CHECK(out[0]==1);CHECK(out[1]==-1);
 pcm.samples.clear();CHECK_FALSE(m.play(pcm,1,0).has_value());CHECK_FALSE(m.play(pcm,-1,0).has_value());
}
}
TEST_SUITE("audio.mixer") {
TEST_CASE("repeating PCM keeps completion tied to its last repeated sample") {
 Mixer m(2);PcmAudio pcm;pcm.rate=2;pcm.channels=1;pcm.frames=2;pcm.samples={16384,-16384};auto id=m.play(pcm,1,0,2);REQUIRE(id.has_value());std::vector<float> out(6);m.mix(out);CHECK(out==std::vector<float>{.5f,.5f,-.5f,-.5f,.5f,.5f});CHECK_FALSE(m.finished(id.value()));out.resize(2);m.mix(out);CHECK(m.finished(id.value()));
}
}
