#include "data/flc.h"
#include "data/byte_reader.h"
#include "data/grp.h"
#include <algorithm>
#include <numeric>
namespace hl {
Result<FlcDecoder> FlcDecoder::open(ByteView b,std::string resource){
 auto fail=[&](size_t p,std::string why)->Result<FlcDecoder>{return DataError{"",resource,p,std::move(why)};};if(b.size()<128)return fail(0,"short FLC header");ByteReader h(b);auto size=h.u32le().value();auto magic=h.u16le().value(),count=h.u16le().value(),w=h.u16le().value(),height=h.u16le().value(),depth=h.u16le().value();h.take(2);auto delay=h.u32le().value();
 if(size!=b.size()||magic!=0xaf12||!count||!w||!height||uint64_t(w)*height>16777216)return fail(0,"invalid FLC size/magic/dimensions/count");
 bool exception=(w==640&&height==480)&&((ascii_key(resource)=="in-pr-mo.flc"&&count==45&&delay==0)||(ascii_key(resource)=="in-pre-m.flc"&&count==200&&delay==0)||(ascii_key(resource)=="mapa7-bl.flc"&&count==1&&delay==71));
 if((depth!=8&&!(depth==0&&exception))||(!delay&&!exception))return fail(12,"unresolved FLC depth/delay compatibility case");
 FlcDecoder out;out.resource_=resource;out.width_=w;out.height_=height;out.count_=count;out.depth_=depth;out.delay_=delay;size_t p=128;
 while(p<b.size()){
  if(!checked_range(b.size(),p,6))return fail(p,"truncated FLC record");ByteReader r(b.subspan(p));auto n=r.u32le().value();auto kind=r.u16le().value();if(n<6||!checked_range(b.size(),p,n))return fail(p,"FLC record outside file");
  if(kind==0xf100){
   if(!out.frames_.empty())return fail(p,"late FLC prefix");size_t q=p+6;while(q<p+n){if(p+n-q<6)return fail(q,"truncated prefix chunk");ByteReader sub(b.subspan(q,p+n-q));auto sn=sub.u32le().value();auto st=sub.u16le().value();if(sn<6||sn>p+n-q||st!=2)return fail(q,"unknown or invalid prefix metadata");q+=sn;}
  }else if(kind==0xf1fa){
   if(n<16)return fail(p,"short frame header");auto chunks=r.u16le().value(),frame_delay=r.u16le().value();size_t q=p+16;for(unsigned i=0;i<chunks;++i){if(p+n-q<6)return fail(q,"short chunk header");ByteReader c(b.subspan(q,p+n-q));auto cn=c.u32le().value();if(cn<6||cn>p+n-q)return fail(q,"chunk outside frame");q+=cn;}
   for(;q<p+n;++q)if(b[q]!=std::byte{})return fail(q,"nonzero unexplained frame tail");out.frames_.push_back({p,n,chunks,frame_delay});
  }else return fail(p,"unknown FLC record type");p+=n;
 }
 if(out.frames_.size()<count||out.frames_.size()>size_t(count)+1)return fail(6,"visible/ring frame count mismatch");
 out.bytes_.assign(b.begin(),b.end());out.state_.indices.resize(size_t(w)*height);out.state_.logical_width=w;out.state_.logical_height=height;out.state_.source={out.resource_,0,0,0,w,height};return out;
}
Result<void> FlcDecoder::decode(const Record& frame){
 size_t p=frame.offset+16;auto fail=[&](size_t at,std::string why)->Result<void>{return DataError{"",resource_,at,"frame "+std::to_string(next_)+": "+why};};
 for(unsigned ci=0;ci<frame.chunks;++ci){ByteReader head(ByteView(bytes_).subspan(p,frame.offset+frame.size-p));auto size=head.u32le().value();auto kind=head.u16le().value();ByteReader r(ByteView(bytes_).subspan(p+6,size-6));
  if(kind==4||kind==11){
   auto packets=r.u16le();if(!packets.has_value())return fail(p,"truncated palette packet count");size_t index=0;
   for(unsigned i=0;i<packets.value();++i){auto skip=r.u8(),count=r.u8();if(!count.has_value())return fail(p,"truncated palette packet");index+=skip.value();size_t n=count.value()?count.value():256;if(index>256||n>256-index)return fail(p,"palette packet out of range");auto rgb=r.take(n*3);if(!rgb.has_value())return fail(p,"truncated palette");for(size_t j=0;j<n;++j)for(size_t c=0;c<3;++c){auto v=std::to_integer<uint8_t>(rgb.value()[j*3+c]);if(kind==11&&v>63)return fail(p,"invalid 6-bit palette value");state_.palette[index+j][c]=uint8_t(kind==11?v*4:v);}index+=n;}
  }else if(kind==13){if(r.remaining())return fail(p,"BLACK chunk has payload");std::fill(state_.indices.begin(),state_.indices.end(),0);
  }else if(kind==16){auto pixels=r.take(state_.indices.size());if(!pixels.has_value())return fail(p,"truncated COPY");for(size_t i=0;i<pixels.value().size();++i)state_.indices[i]=std::to_integer<uint8_t>(pixels.value()[i]);
  }else if(kind==15){
   for(size_t y=0;y<height_;++y){if(!r.u8().has_value())return fail(p,"truncated BRUN row");size_t x=0;while(x<width_){auto raw=r.u8();if(!raw.has_value())return fail(p,"truncated BRUN run");int count=int8_t(raw.value());if(count==0)return fail(p,"zero BRUN run");size_t n=std::abs(count);if(n>width_-x)return fail(p,"BRUN run crosses line");if(count>0){auto v=r.u8();if(!v.has_value())return fail(p,"truncated BRUN repeat");std::fill_n(state_.indices.begin()+y*width_+x,n,v.value());}else{auto v=r.take(n);if(!v.has_value())return fail(p,"truncated BRUN literals");for(size_t i=0;i<n;++i)state_.indices[y*width_+x+i]=std::to_integer<uint8_t>(v.value()[i]);}x+=n;}}
  }else if(kind==12){
   auto skip=r.u16le(),lines=r.u16le();if(!lines.has_value()||skip.value()>height_||lines.value()>height_-skip.value())return fail(p,"invalid LC line range");
   for(size_t y=skip.value();y<size_t(skip.value())+lines.value();++y){auto packets=r.u8();if(!packets.has_value())return fail(p,"truncated LC packets");size_t x=0;for(unsigned i=0;i<packets.value();++i){auto skip_x=r.u8(),raw=r.u8();if(!raw.has_value())return fail(p,"truncated LC packet");if(skip_x.value()>width_-x)return fail(p,"LC skip crosses line");x+=skip_x.value();int count=int8_t(raw.value());size_t n=std::abs(count);if(n>width_-x)return fail(p,"LC run crosses line");if(count<0){auto v=r.u8();if(!v.has_value())return fail(p,"short LC repeat");std::fill_n(state_.indices.begin()+y*width_+x,n,v.value());}else{auto v=r.take(n);if(!v.has_value())return fail(p,"short LC literal");for(size_t j=0;j<n;++j)state_.indices[y*width_+x+j]=std::to_integer<uint8_t>(v.value()[j]);}x+=n;}}
  }else if(kind==7){
   auto lines=r.u16le();if(!lines.has_value())return fail(p,"short SS2 line count");size_t y=0,done=0;
   while(done<lines.value()){
    auto op=r.u16le();if(!op.has_value())return fail(p,"truncated SS2 line control");auto top=op.value()&0xc000;
    if(top==0xc000){size_t skip=-int(int16_t(op.value()));if(y>height_||skip>height_-y)return fail(p,"SS2 skip crosses frame");y+=skip;continue;}
    if(y>=height_)return fail(p,"SS2 line outside frame");
    if(top==0x8000){state_.indices[y*width_+width_-1]=uint8_t(op.value());continue;}
    if(top)return fail(p,"reserved SS2 line control");size_t x=0;
    for(unsigned i=0;i<op.value();++i){auto skip=r.u8(),raw=r.u8();if(!raw.has_value())return fail(p,"truncated SS2 packet");if(skip.value()>width_-x)return fail(p,"SS2 skip crosses line");x+=skip.value();int count=int8_t(raw.value());size_t n=std::abs(count)*2;if(n>width_-x)return fail(p,"SS2 run crosses line");if(count<0){auto pair=r.take(2);if(!pair.has_value())return fail(p,"truncated SS2 pair");for(size_t j=0;j<n;++j)state_.indices[y*width_+x+j]=std::to_integer<uint8_t>(pair.value()[j%2]);}else{auto values=r.take(n);if(!values.has_value())return fail(p,"truncated SS2 literals");for(size_t j=0;j<n;++j)state_.indices[y*width_+x+j]=std::to_integer<uint8_t>(values.value()[j]);}x+=n;}
    ++done;++y;
   }
  }else if(kind==18){
   // PSTAMP is a nonvisual thumbnail; the enclosing bounded size is authoritative.
   if(r.remaining()<6)return fail(p,"short PSTAMP thumbnail header");r.take(r.remaining());
  }else return fail(p,"unknown or unimplemented visual chunk "+std::to_string(kind));
  if(r.remaining()>1)return fail(p,"unconsumed chunk payload");p+=size;
 }return {};
}
Result<std::optional<IndexedFrame>> FlcDecoder::next(){
 if(next_>=count_){if(next_<frames_.size()){auto r=decode(frames_[next_]);if(!r.has_value())return r.error();++next_;}return std::optional<IndexedFrame>{};}
 auto r=decode(frames_[next_]);if(!r.has_value())return r.error();state_.frame_index=next_;state_.source.frame=next_;state_.declared_delay={delay_,1000};
 // DOS loader 0x1cdcc floors speed*35/1000; scheduler 0x138fc advances on equality before increment.
 state_.effective_delay={uint64_t(delay_)*35/1000+1,1,DelayUnit::engine_updates};++next_;return std::optional<IndexedFrame>{state_};
}
}

namespace hl {
Result<void> FlcDecoder::rewind(){next_=0;std::fill(state_.indices.begin(),state_.indices.end(),0);state_.palette={};return {};}
Result<IndexedFrame> FlcDecoder::seek(uint32_t index){if(index>=count_)return DataError{"",resource_,index,"frame outside visible range"};rewind();for(uint32_t i=0;i<=index;++i){auto f=next();if(!f.has_value())return f.error();if(i==index)return std::move(f.value().value());}return DataError{"",resource_,index,"unreachable seek"};}
Result<void> FrameClock::advance(FrameDelay d){
 auto fail=[]()->Result<void>{return DataError{"","clock",0,"invalid or overflowing rational delay"};};if(!d.denominator||d.unit!=DelayUnit::seconds)return fail();auto g=std::gcd(denominator_,d.denominator);auto a=d.denominator/g,b=denominator_/g;
 if(denominator_>UINT64_MAX/a||numerator_>UINT64_MAX/a||d.numerator>UINT64_MAX/b)return fail();auto left=numerator_*a,right=d.numerator*b;if(right>UINT64_MAX-left)return fail();auto sum=left+right,denom=denominator_*a;auto reduce=std::gcd(sum,denom);sum/=reduce;denom/=reduce;
 if(sum/denom>UINT64_MAX/1000000000||sum%denom>UINT64_MAX/1000000000)return fail();numerator_=sum;denominator_=denom;return {};
}
uint64_t FrameClock::elapsed_nanoseconds()const{return (numerator_/denominator_)*1000000000+(numerator_%denominator_)*1000000000/denominator_;}
}
