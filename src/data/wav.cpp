#include "data/wav.h"
#include "data/byte_reader.h"
#include <optional>
namespace hl {
Result<PcmAudio> decode_wav(ByteView b){
 auto fail=[](size_t p,std::string why)->Result<PcmAudio>{return DataError{"","WAV",p,std::move(why)};};if(b.size()<12)return fail(0,"short RIFF header");auto tag=[&](size_t p){return std::string(reinterpret_cast<const char*>(b.data()+p),4);};ByteReader h(b.subspan(4));auto size=h.u32le().value();if(tag(0)!="RIFF"||tag(8)!="WAVE"||uint64_t(size)+8!=b.size())return fail(0,"invalid RIFF size/type");
 PcmAudio out;std::optional<ByteView> data;bool have_fmt=false;size_t p=12;
 while(p<b.size()){
  if(b.size()-p<8)return fail(p,"short WAV chunk header");ByteReader r(b.subspan(p+4));auto n=r.u32le().value();size_t payload=p+8;if(!checked_range(b.size(),payload,uint64_t(n)+(n&1)))return fail(p,"WAV chunk/padding outside file");auto kind=tag(p);
  if(kind=="fmt "){
   if(have_fmt||n<16)return fail(p,"short or repeated fmt");have_fmt=true;ByteReader f(b.subspan(payload,n));auto codec=f.u16le().value(),channels=f.u16le().value();auto rate=f.u32le().value(),byte_rate=f.u32le().value();auto alignment=f.u16le().value(),bits=f.u16le().value();
   if(codec!=1||bits!=16||channels<1||channels>2||rate==0||rate>384000||alignment!=channels*2||uint64_t(rate)*alignment!=byte_rate)return fail(payload,"unsupported WAV codec or invalid PCM alignment");out.rate=rate;out.channels=channels;out.channel_order=channels==1?"mono":"left,right";
  }else if(kind=="data"){if(data)return fail(p,"repeated data chunk");data=b.subspan(payload,n);}
  p=payload+n+(n&1);
 }
 if(!have_fmt||!data||data->size()%(out.channels*2))return fail(p,"missing fmt/data or incomplete sample frame");out.frames=data->size()/(out.channels*2);ByteReader pcm(*data);out.samples.reserve(data->size()/2);while(pcm.remaining())out.samples.push_back(static_cast<int16_t>(pcm.u16le().value()));return out;
}
}
