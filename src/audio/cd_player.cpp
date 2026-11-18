#include "audio/cd_player.h"
#include "data/grp.h"
#include "dr_flac.h"
#include <array>
#include <algorithm>
namespace hl {
struct CdPlayer::State {
 std::filesystem::path file;drflac* decoder{};unsigned track{};uint64_t sample{},fraction{},begin{},buffered{};uint32_t output_rate{};bool loop{},active{};
 std::array<int16_t,8192> buffer{};
 ~State(){drflac_close(decoder);}
};
CdPlayer::CdPlayer()=default;CdPlayer::~CdPlayer()=default;
Result<void> CdPlayer::play(unsigned track,const std::filesystem::path& file,bool loop) {
 if(track<2||ascii_key(file.filename().string()).find("pregap")!=std::string::npos)return DataError{"","CD",0,"CD playback requires audio track INDEX01"};
 auto next=std::make_unique<State>();
#ifdef _WIN32
 next->decoder=drflac_open_file_w(file.c_str(),nullptr);
#else
 next->decoder=drflac_open_file(file.c_str(),nullptr);
#endif
 if(!next->decoder)return DataError{"",file.filename().string(),0,"cannot open CD FLAC"};
 if(next->decoder->sampleRate!=44100||next->decoder->channels!=2||next->decoder->bitsPerSample!=16||!next->decoder->totalPCMFrameCount)return DataError{"","CD",0,"invalid CD PCM format"};
 next->file=file;next->track=track;next->loop=loop;next->active=true;state_=std::move(next);if(device_){auto r=device_->play(track,file,loop);if(!r.has_value())device_error_=r.error();}return {};
}
Result<void> CdPlayer::seek(uint64_t sample){
 auto r=seek_local(sample);if(!r.has_value())return r;
 if(device_){r=device_->seek(sample);if(!r.has_value())device_error_=r.error();}return {};
}
Result<void> CdPlayer::seek_local(uint64_t sample) {
 if(!state_||sample>state_->decoder->totalPCMFrameCount)return DataError{"","CD",size_t(sample),"CD seek outside track"};
 if(!drflac_seek_to_pcm_frame(state_->decoder,sample))return DataError{"","CD",size_t(sample),"CD seek failed"};
 state_->sample=sample;state_->fraction=0;state_->buffered=0;return {};
}
Result<void> CdPlayer::mix(std::span<float> output,uint32_t rate) {
 std::fill(output.begin(),output.end(),0);if(!rate||rate>384000||output.size()%2)return DataError{"","CD",0,"invalid CD output format"};
 if(!state_||!state_->active)return {};auto& s=*state_;
 if(s.output_rate&&s.output_rate!=rate)return DataError{"","CD",0,"CD output rate changed during playback"};s.output_rate=rate;
 for(size_t i=0;i<output.size()/2;++i) {
  if(s.sample>=s.decoder->totalPCMFrameCount) {
   if(!s.loop){s.active=false;break;}
   auto remainder=s.fraction;auto sample=s.sample%s.decoder->totalPCMFrameCount;auto r=seek_local(sample);if(!r.has_value())return r;s.fraction=remainder;
  }
  if(!s.buffered||s.sample<s.begin||s.sample>=s.begin+s.buffered) {
   if(s.sample!=s.decoder->currentPCMFrame&&!drflac_seek_to_pcm_frame(s.decoder,s.sample))return DataError{"","CD",size_t(s.sample),"CD refill seek failed"};
   s.begin=s.sample;s.buffered=drflac_read_pcm_frames_s16(s.decoder,4096,s.buffer.data());
   if(!s.buffered)return DataError{"","CD",size_t(s.sample),"truncated CD stream"};
  }
  for(size_t c=0;c<2;++c)output[i*2+c]=float(s.buffer[size_t(s.sample-s.begin)*2+c])/32768;
  s.fraction+=44100;s.sample+=s.fraction/rate;s.fraction%=rate;
  if(s.sample>=s.decoder->totalPCMFrameCount&&!s.loop){s.sample=s.decoder->totalPCMFrameCount;s.active=false;}
 }
 return {};
}
Result<void> CdPlayer::mix_device(std::span<float> output,uint32_t rate){
 if(device_error_)return *device_error_;
 if(!device_){
  device_=std::make_unique<CdPlayer>();
  if(state_){auto r=device_->play(state_->track,state_->file,state_->loop);if(!r.has_value())return r;r=device_->seek(state_->sample);if(!r.has_value())return r;if(!state_->active)device_->stop();}
 }
 return device_->mix(output,rate);
}
Result<void> CdPlayer::resume(){if(!state_||state_->sample>=state_->decoder->totalPCMFrameCount)return DataError{"","CD",0,"no paused CD track to resume"};state_->active=true;if(device_){auto r=device_->seek(state_->sample);if(r.has_value())r=device_->resume();if(!r.has_value())device_error_=r.error();}return {};}
void CdPlayer::stop(){if(state_)state_->active=false;if(device_)device_->stop();}
unsigned CdPlayer::track()const{return state_?state_->track:0;}
uint64_t CdPlayer::position()const{return state_?state_->sample:0;}
bool CdPlayer::playing()const{return state_&&state_->active;}
}
