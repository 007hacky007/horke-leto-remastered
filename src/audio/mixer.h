#pragma once
#include "data/wav.h"
#include <map>
namespace hl {
// Original WAV normalization, volume scaling and four-channel headroom.
void prepare_dos_sample(PcmAudio& pcm,int volume);
// Device cursors advance in real time without changing script/save timing.
class Mixer {
 struct Voice {PcmAudio pcm;uint64_t phase{},device_phase{},device_delay{};float gain{},pan{};bool stopped{};uint32_t repeats{1};int32_t priority{100};size_t slot{};};
 size_t capacity_;
 uint32_t rate_;uint64_t next_{};std::map<uint64_t,Voice> voices_;
 bool device_active_{};std::vector<std::pair<uint64_t,Voice>> device_tails_;
 void mix_output(std::span<float>,bool device);
public:
 explicit Mixer(uint32_t rate=48000,size_t capacity=64):capacity_(capacity),rate_(rate){}
 Result<uint64_t> play(const PcmAudio& pcm,float gain=1,float pan=0,uint32_t repeats=1,int32_t priority=100,uint64_t device_after=0);
 void mix(std::span<float> interleaved_output);
 void mix_device(std::span<float> interleaved_output);
 bool finished(uint64_t id)const;
 uint64_t position(uint64_t id)const;
 void stop(uint64_t id);
 void stop_all();
};
}
