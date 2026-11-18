#pragma once
#include <SDL3/SDL.h>
namespace hl {
struct DiskAudioClock {Uint64 start{},frames{};bool paced{};};
inline void SDLCALL pace_disk_audio(void* userdata,const SDL_AudioSpec* spec,float*,int bytes){
 auto& clock=*static_cast<DiskAudioClock*>(userdata);auto now=SDL_GetTicksNS();
 if(!clock.start)clock.start=now;
 auto deadline=clock.start+(clock.frames/spec->freq)*1000000000+(clock.frames%spec->freq)*1000000000/spec->freq;
 if(deadline>now)SDL_DelayPrecise(deadline-now);
 clock.frames+=bytes/(spec->channels*int(sizeof(float)));
}
inline SDL_AudioStream* open_audio_output(DiskAudioClock& clock,SDL_AudioStreamCallback callback=nullptr,void* userdata=nullptr){
 // ~11 ms: low enough that voices stay with their subtitles, large enough not to crackle while an update holds the lock.
 SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,"512",SDL_HINT_DEFAULT);
 clock={};
 bool disk=SDL_strcmp(SDL_GetCurrentAudioDriver(),"disk")==0;
 // Disk's integer relative sleeps accumulate rounding and scheduler drift.
 if(disk)clock.paced=SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_TIMESCALE,"0",SDL_HINT_DEFAULT);
 SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
 auto stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,callback,userdata);
 if(stream&&clock.paced){
  if(!SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(stream),pace_disk_audio,&clock)){SDL_DestroyAudioStream(stream);return nullptr;}
 }
 if(stream&&!SDL_ResumeAudioStreamDevice(stream)){SDL_DestroyAudioStream(stream);return nullptr;}
 return stream;
}
}
