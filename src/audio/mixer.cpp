#include "audio/mixer.h"
#include <algorithm>
#include <cmath>
namespace hl {
Result<uint64_t> Mixer::play(const PcmAudio& pcm,float gain,float pan,uint32_t repeats,int32_t priority) {
 if(!repeats||pcm.frames>UINT64_MAX/repeats||!rate_||!pcm.rate||pcm.rate>384000||!pcm.frames||pcm.channels<1||pcm.channels>2||pcm.frames>UINT64_MAX/pcm.channels||pcm.samples.size()!=pcm.frames*pcm.channels||!std::isfinite(gain)||!std::isfinite(pan)||gain<0||gain>1||pan<-1||pan>1)return DataError{"","mixer",0,"invalid PCM/gain/pan"};
 for(auto i=voices_.begin();i!=voices_.end();)if(finished(i->first))i=voices_.erase(i);else ++i;
 if(!capacity_||capacity_>64)return DataError{"","mixer",0,"invalid voice capacity"};
 // DOS24041: lower numbers win, and ties replace the first worst channel.
 if(voices_.size()>=capacity_){auto worst=std::max_element(voices_.begin(),voices_.end(),[](const auto& a,const auto& b){return a.second.priority<b.second.priority||(a.second.priority==b.second.priority&&a.second.slot>b.second.slot);});if(priority>worst->second.priority)return uint64_t{0};voices_.erase(worst);}
 size_t slot=0;while(std::any_of(voices_.begin(),voices_.end(),[&](const auto& v){return v.second.slot==slot;}))++slot;
 auto id=++next_;voices_.emplace(id,Voice{pcm,0,gain,pan,false,repeats,priority,slot});return id;
}
void Mixer::mix(std::span<float> output) {
 std::fill(output.begin(),output.end(),0);if(output.size()%2||!rate_)return;
 for(auto& [id,v]:voices_) {
  if(v.stopped)continue;
  for(size_t i=0;i<output.size()/2;++i) {
   auto frame=v.phase/rate_;if(frame>=v.pcm.frames*v.repeats)break;frame%=v.pcm.frames;
   for(size_t c=0;c<2;++c){auto sample=v.pcm.samples[size_t(frame)*v.pcm.channels+(v.pcm.channels==1?0:c)];float channel=c==0?std::min(1.f,1-v.pan):std::min(1.f,1+v.pan);output[i*2+c]+=float(sample)/32768*v.gain*channel;}
   v.phase+=v.pcm.rate;
  }
 }
 for(auto& sample:output)sample=std::clamp(sample,-1.f,1.f);
}
bool Mixer::finished(uint64_t id)const {auto i=voices_.find(id);return i==voices_.end()||i->second.stopped||i->second.phase/rate_>=i->second.pcm.frames*i->second.repeats;}
uint64_t Mixer::position(uint64_t id)const {auto i=voices_.find(id);return i==voices_.end()?0:std::min(i->second.phase/rate_,i->second.pcm.frames);}
void Mixer::stop(uint64_t id){auto i=voices_.find(id);if(i!=voices_.end())i->second.stopped=true;}
void Mixer::stop_all(){for(auto& [id,v]:voices_)v.stopped=true;}
}
