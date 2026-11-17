#include "data/flac.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
namespace hl {
Result<PcmAudio> decode_flac(const std::filesystem::path& path){
#ifdef _WIN32
 drflac* d=drflac_open_file_w(path.c_str(),nullptr);
#else
 drflac* d=drflac_open_file(path.c_str(),nullptr);
#endif
 if(!d)return DataError{"",path.filename().string(),0,"cannot decode FLAC"};
 if(d->channels<1||d->channels>2||d->bitsPerSample!=16||d->totalPCMFrameCount>UINT64_C(1000000000)){drflac_close(d);return DataError{"",path.filename().string(),0,"invalid/excessive FLAC format"};}
 PcmAudio out;out.rate=d->sampleRate;out.channels=d->channels;out.frames=d->totalPCMFrameCount;out.channel_order=out.channels==1?"mono":"left,right";out.samples.resize(size_t(out.frames)*out.channels);auto read=drflac_read_pcm_frames_s16(d,out.frames,out.samples.data());drflac_close(d);if(read!=out.frames)return DataError{"",path.filename().string(),0,"truncated FLAC PCM"};return out;
}
}
