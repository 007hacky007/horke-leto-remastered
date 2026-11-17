#pragma once
#include "data/wav.h"
#include <map>
namespace hl {
class Mixer {
 struct Voice {PcmAudio pcm;uint64_t phase{};float gain{},pan{};bool stopped{};uint32_t repeats{1};int32_t priority{100};size_t slot{};};
 size_t capacity_;
 uint32_t rate_;uint64_t next_{};std::map<uint64_t,Voice> voices_;
public:
 explicit Mixer(uint32_t rate=48000,size_t capacity=64):capacity_(capacity),rate_(rate){}
 Result<uint64_t> play(const PcmAudio& pcm,float gain=1,float pan=0,uint32_t repeats=1,int32_t priority=100);
 void mix(std::span<float> interleaved_output);
 bool finished(uint64_t id)const;
 uint64_t position(uint64_t id)const;
 void stop(uint64_t id);
 void stop_all();
};
}
