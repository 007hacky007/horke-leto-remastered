#include "audio/mixer.h"
#include <algorithm>
#include <cmath>
namespace hl {
void prepare_dos_sample(PcmAudio& pcm,int volume) {
 int peak=0;for(auto sample:pcm.samples)peak=std::max(peak,std::abs(int(sample)));
 if(!peak)return;volume=std::clamp(volume,0,100);
 // DOS275f1 normalizes to 0x7ff8; DOS235f5 shifts each voice before summing.
 for(auto& sample:pcm.samples)sample=int16_t(((int(sample)*32760/peak)*volume/100)>>2);
}

Result<uint64_t> Mixer::play(const PcmAudio& pcm,float gain,float pan,uint32_t repeats,int32_t priority,uint64_t device_after) {
 if(!repeats||pcm.frames>UINT64_MAX/repeats||!rate_||!pcm.rate||pcm.rate>384000||!pcm.frames||pcm.channels<1||pcm.channels>2||pcm.frames>UINT64_MAX/pcm.channels||pcm.samples.size()!=pcm.frames*pcm.channels||!std::isfinite(gain)||!std::isfinite(pan)||gain<0||gain>1||pan<-1||pan>1)return DataError{"","mixer",0,"invalid PCM/gain/pan"};
 uint64_t delay=0;auto before=voices_.find(device_after);const Voice* predecessor=before==voices_.end()?nullptr:&before->second;
 if(!predecessor)for(const auto& [id,v]:device_tails_)if(id==device_after){predecessor=&v;break;}
 if(device_active_&&predecessor&&!predecessor->stopped){const auto& v=*predecessor;auto end=v.pcm.frames*v.repeats*rate_;delay=v.device_delay+(end>v.device_phase?(end-v.device_phase+v.pcm.rate-1)/v.pcm.rate:0);}
 // Keep a final device buffer outside the VM channel/priority accounting.
 for(auto i=voices_.begin();i!=voices_.end();)if(finished(i->first)){
  auto& v=i->second;if(device_active_&&!v.stopped&&v.device_phase/rate_<v.pcm.frames*v.repeats)device_tails_.emplace_back(i->first,std::move(v));i=voices_.erase(i);
 }else ++i;
 if(!capacity_||capacity_>64)return DataError{"","mixer",0,"invalid voice capacity"};
 // DOS24041: lower numbers win, and ties replace the first worst channel.
 if(voices_.size()>=capacity_){auto worst=std::max_element(voices_.begin(),voices_.end(),[](const auto& a,const auto& b){return a.second.priority<b.second.priority||(a.second.priority==b.second.priority&&a.second.slot>b.second.slot);});if(priority>worst->second.priority)return uint64_t{0};voices_.erase(worst);}
 size_t slot=0;while(std::any_of(voices_.begin(),voices_.end(),[&](const auto& v){return v.second.slot==slot;}))++slot;
 auto id=++next_;voices_.emplace(id,Voice{pcm,0,0,delay,gain,pan,false,repeats,priority,slot});return id;
}
void Mixer::mix(std::span<float> output){mix_output(output,false);}
void Mixer::mix_device(std::span<float> output){
 if(!device_active_){for(auto& [id,v]:voices_)v.device_phase=v.phase;device_active_=true;}
 mix_output(output,true);
 std::erase_if(device_tails_,[&](const auto& v){return v.second.stopped||v.second.device_phase/rate_>=v.second.pcm.frames*v.second.repeats;});
}
void Mixer::mix_output(std::span<float> output,bool device) {
 std::fill(output.begin(),output.end(),0);if(output.size()%2||!rate_)return;
 auto mix_voice=[&](Voice& v){
  if(v.stopped)return;auto& phase=device?v.device_phase:v.phase;
  for(size_t i=0;i<output.size()/2;++i) {
   if(device&&v.device_delay){--v.device_delay;continue;}
   auto frame=phase/rate_;if(frame>=v.pcm.frames*v.repeats)break;frame%=v.pcm.frames;
   for(size_t c=0;c<2;++c){auto sample=v.pcm.samples[size_t(frame)*v.pcm.channels+(v.pcm.channels==1?0:c)];float channel=c==0?std::min(1.f,1-v.pan):std::min(1.f,1+v.pan);output[i*2+c]+=float(sample)/32768*v.gain*channel;}
   phase+=v.pcm.rate;
  }
 };
 for(auto& [id,v]:voices_)mix_voice(v);
 if(device)for(auto& [id,v]:device_tails_)mix_voice(v);
 for(auto& sample:output)sample=std::clamp(sample,-1.f,1.f);
}
bool Mixer::finished(uint64_t id)const {auto i=voices_.find(id);return i==voices_.end()||i->second.stopped||i->second.phase/rate_>=i->second.pcm.frames*i->second.repeats;}
uint64_t Mixer::position(uint64_t id)const {auto i=voices_.find(id);return i==voices_.end()?0:std::min(i->second.phase/rate_,i->second.pcm.frames);}
void Mixer::stop(uint64_t id){auto i=voices_.find(id);if(i!=voices_.end())i->second.stopped=true;for(auto& [tail_id,v]:device_tails_)if(tail_id==id)v.stopped=true;}
void Mixer::stop_all(){for(auto& [id,v]:voices_)v.stopped=true;device_tails_.clear();}
}
