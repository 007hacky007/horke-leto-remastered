#include "data/disc.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <charconv>
namespace hl {
bool safe_relative_path(const std::filesystem::path& p){
 if(p.empty()||p.is_absolute()||p.has_root_name())return false;
 auto s=p.generic_string();if(s.find('\\')!=s.npos||s.find(':')!=s.npos||s.find('\0')!=s.npos)return false;
 for(const auto& part:p)if(part==".."||part=="."||part.empty())return false;
 return true;
}
Result<DiscLayout> parse_cue(std::string_view text){
 DiscLayout out;std::istringstream lines{std::string(text)};std::string line;size_t offset=0;bool have_index=false;
 auto fail=[&](std::string why)->Result<DiscLayout>{return DataError{"","CUE",offset,std::move(why)};};
 while(std::getline(lines,line)){
  std::istringstream row(line);std::string kind;row>>kind;
  if(kind.empty()||kind=="REM"){offset+=line.size()+1;continue;}
  if(kind=="FILE"){
   std::string name,type;if(!out.bin.empty()||!(row>>std::quoted(name)>>type)||type!="BINARY"||!safe_relative_path(name))return fail("missing, multiple, or unsafe BIN declaration");out.bin=name;
  }else if(kind=="TRACK"){
   unsigned number;std::string type;if(out.bin.empty()||(!out.tracks.empty()&&!have_index)||!(row>>number>>type)||(type!="AUDIO"&&type!="MODE1/2352")||number!=out.tracks.size()+1)return fail("invalid TRACK");
   if(type=="MODE1/2352"&&!out.tracks.empty())return fail("data track must be first");out.tracks.push_back({number,type=="MODE1/2352",0,{},0});have_index=false;
  }else if(kind=="INDEX"||kind=="PREGAP"){
   if(out.tracks.empty())return fail("index outside track");unsigned index=0;std::string time;if(kind=="INDEX"&&(!(row>>index)||index>1))return fail("unsupported INDEX");if(!(row>>time))return fail("missing time");
   uint64_t values[3]{};size_t start=0;
   for(int i=0;i<3;++i){auto end=i==2?time.size():time.find(':',start);if(end==time.npos||end==start)return fail("invalid MM:SS:FF");auto r=std::from_chars(time.data()+start,time.data()+end,values[i]);if(r.ec!=std::errc{}||r.ptr!=time.data()+end)return fail("invalid time");start=end+1;}
   if(values[1]>=60||values[2]>=75||values[0]>(UINT64_MAX-4499)/4500)return fail("invalid time range");auto sector=values[0]*4500+values[1]*75+values[2];auto& t=out.tracks.back();
   if(kind=="PREGAP"){if(t.pregap||have_index)return fail("duplicate or late pregap");t.pregap=sector;}
   else if(index==0){if(t.index00||have_index)return fail("invalid INDEX 00");t.index00=sector;}
   else{if(have_index||(t.index00&&*t.index00>sector)|| (out.tracks.size()>1&&sector<=out.tracks[out.tracks.size()-2].index01))return fail("unordered INDEX 01");t.index01=sector;have_index=true;}
  }else return fail("unsupported CUE directive: "+kind);
  std::string extra;if(row>>extra)return fail("trailing CUE fields");offset+=line.size()+1;
 }
 if(out.bin.empty()||out.tracks.empty()||!have_index)return fail("incomplete CUE");
 for(size_t i=1;i<out.tracks.size();++i){auto boundary=out.tracks[i].index00.value_or(out.tracks[i].index01);if(boundary<=out.tracks[i-1].index01)return fail("overlapping track index boundaries");}
 return out;
}
Result<DiscReader> DiscReader::open(const std::filesystem::path& cue){
 std::ifstream f(cue,std::ios::binary);if(!f)return DataError{cue.string(),"",0,"cannot open CUE"};std::string text((std::istreambuf_iterator<char>(f)),{});auto parsed=parse_cue(text);if(!parsed.has_value())return parsed.error();
 std::error_code ec;auto root=std::filesystem::canonical(cue.parent_path().empty()?".":cue.parent_path(),ec);if(ec)return DataError{cue.string(),"",0,"invalid CUE directory"};auto bin=std::filesystem::canonical(root/parsed.value().bin,ec);
 if(ec||!safe_relative_path(bin.lexically_relative(root))||!std::filesystem::is_regular_file(bin))return DataError{cue.string(),"",0,"missing or escaping BIN"};auto size=std::filesystem::file_size(bin,ec);
 if(ec||size%2352)return DataError{cue.string(),"",0,"partial physical sector"};
 auto& layout=parsed.value();layout.bin=bin;layout.sectors=size/2352;
 for(auto& t:layout.tracks)if(t.index01>=layout.sectors||(t.index00&&*t.index00>=layout.sectors))return DataError{cue.string(),"",0,"track outside BIN"};
 DiscReader out;out.layout_=std::move(layout);return out;
}
uint64_t DiscReader::data_sectors()const{if(layout_.tracks.empty()||!layout_.tracks[0].mode1)return 0;auto begin=layout_.tracks[0].index01;auto end=layout_.tracks.size()>1?layout_.tracks[1].index00.value_or(layout_.tracks[1].index01):layout_.sectors;return end>=begin?end-begin:0;}
Result<std::array<std::byte,2048>> DiscReader::read_mode1_sector(uint64_t lba)const{
 if(lba>=data_sectors())return DataError{layout_.bin.string(),"MODE1",size_t(lba),"sector outside data track"};
 auto physical=lba+layout_.tracks[0].index01;std::ifstream f(layout_.bin,std::ios::binary);f.seekg(static_cast<std::streamoff>(physical*2352+16));std::array<std::byte,2048> out{};f.read(reinterpret_cast<char*>(out.data()),out.size());if(!f)return DataError{layout_.bin.string(),"MODE1",size_t(physical*2352+16),"short sector"};return out;
}
}
#include "data/byte_reader.h"
#include <set>
namespace hl { namespace {
uint32_t le32(ByteView b,size_t p){ByteReader r(b.subspan(p,4));return r.u32le().value();}
uint32_t be32(ByteView b,size_t p){uint32_t v=0;for(size_t i=0;i<4;++i)v=(v<<8)|std::to_integer<uint8_t>(b[p+i]);return v;}
Result<IsoEntry> iso_record(ByteView b,uint64_t sectors){
 auto fail=[](std::string why)->Result<IsoEntry>{return DataError{"","ISO",0,std::move(why)};};
 if(b.size()<34||std::to_integer<size_t>(b[0])!=b.size())return fail("short directory record");auto n=std::to_integer<size_t>(b[32]);
 if(n==0||!checked_range(b.size(),33,n))return fail("name outside directory record");
 if(le32(b,2)!=be32(b,6)||le32(b,10)!=be32(b,14))return fail("conflicting dual-endian extent");
 if(b[1]!=std::byte{}||b[26]!=std::byte{}||b[27]!=std::byte{})return fail("extended/interleaved extent unsupported");
 auto flags=std::to_integer<uint8_t>(b[25]);if(flags&0x80)return fail("multi-extent file unsupported");
 IsoEntry e; e.extent={le32(b,2),le32(b,10)};e.directory=(flags&2)!=0;auto valid=validate_iso_extent(e.extent,sectors);if(!valid.has_value())return valid.error();
 if(n==1&&(b[33]==std::byte{}||b[33]==std::byte{1})){e.name=b[33]==std::byte{}?".":"..";return e;}
 e.name=std::string(reinterpret_cast<const char*>(b.data()+33),n);auto suffix=e.name.find(';');if(suffix!=e.name.npos){if(e.name.substr(suffix)!=";1")return fail("unsupported ISO version");e.name.resize(suffix);}
 if(!safe_relative_path(e.name)||std::filesystem::path(e.name).has_parent_path())return fail("unsafe ISO name");return e;
}
}
Result<void> validate_iso_extent(IsoExtent x,uint64_t sectors){auto count=(uint64_t(x.size)+2047)/2048;if(x.lba>sectors||count>sectors-x.lba)return DataError{"","ISO",x.lba,"extent outside data track"};return {};}
Result<std::vector<IsoEntry>> parse_iso_directory(ByteView b,uint64_t sectors){
 std::vector<IsoEntry> out;std::set<std::string> names;size_t p=0;
 while(p<b.size()){auto size=std::to_integer<size_t>(b[p]);if(!size){auto next=std::min(b.size(),(p/2048+1)*2048);for(;p<next;++p)if(b[p]!=std::byte{})return DataError{"","ISO",p,"nonzero directory padding"};continue;}
  if(!checked_range(b.size(),p,size)||size>2048-p%2048)return DataError{"","ISO",p,"directory record crosses extent or sector"};auto e=iso_record(b.subspan(p,size),sectors);if(!e.has_value())return e.error();if(e.value().name!="."&&e.value().name!=".."){if(!names.insert(e.value().name).second)return DataError{"","ISO",p,"duplicate directory name"};out.push_back(std::move(e.value()));}p+=size;
 }return out;
}
Result<std::vector<std::byte>> read_iso_extent(DiscReader& disc,IsoExtent x){auto valid=validate_iso_extent(x,disc.data_sectors());if(!valid.has_value())return valid.error();std::vector<std::byte> out(x.size);for(size_t p=0;p<out.size();p+=2048){auto s=disc.read_mode1_sector(uint64_t(x.lba)+p/2048);if(!s.has_value())return s.error();std::copy_n(s.value().begin(),std::min(size_t(2048),out.size()-p),out.begin()+p);}return out;}
Result<std::vector<IsoEntry>> read_iso_directory(DiscReader& disc,IsoExtent x){if(x.size>16*1024*1024)return DataError{"","ISO",x.lba,"directory too large"};auto b=read_iso_extent(disc,x);if(!b.has_value())return b.error();return parse_iso_directory(b.value(),disc.data_sectors());}
Result<IsoVolume> read_iso_volume(DiscReader& disc){auto s=disc.read_mode1_sector(16);if(!s.has_value())return s.error();auto b=ByteView(s.value());if(b[0]!=std::byte{1}||std::string(reinterpret_cast<const char*>(b.data()+1),5)!="CD001"||b[6]!=std::byte{1}||b[128]!=std::byte{0}||b[129]!=std::byte{8}||b[130]!=std::byte{8}||b[131]!=std::byte{0})return DataError{"","ISO",16,"invalid primary descriptor"};auto root=iso_record(b.subspan(156,std::to_integer<size_t>(b[156])),disc.data_sectors());if(!root.has_value())return root.error();std::string name(reinterpret_cast<const char*>(b.data()+40),32);while(!name.empty()&&name.back()==' ')name.pop_back();return IsoVolume{name,root.value().extent};}
}
namespace hl {
Result<std::vector<CdRange>> cd_track_ranges(const DiscLayout& d){
 if(d.tracks.size()!=3||!d.tracks[0].mode1||d.tracks[1].mode1||d.tracks[2].mode1)return DataError{"","CD",0,"expected one data and two audio tracks"};auto& t2=d.tracks[1];auto& t3=d.tracks[2];auto boundary=t3.index00.value_or(t3.index01);
 if(t2.index01>=boundary||boundary>t3.index01||t3.index01>=d.sectors)return DataError{"","CD",0,"invalid CD index boundaries"};std::vector<CdRange> out{{2,t2.index01,boundary,false},{3,t3.index01,d.sectors,false}};if(boundary<t3.index01)out.push_back({3,boundary,t3.index01,true});return out;
}
Result<std::vector<int16_t>> extract_cd_pcm(const DiscLayout& d,const CdRange& r){
 std::error_code ec;auto size=std::filesystem::file_size(d.bin,ec);if(ec||size%2352||size/2352!=d.sectors||r.begin>=r.end||r.end>d.sectors||(r.end-r.begin)>SIZE_MAX/1176)return DataError{"","CD",0,"invalid CD range or partial physical sector"};
 std::ifstream f(d.bin,std::ios::binary);f.seekg(static_cast<std::streamoff>(r.begin*2352));std::vector<int16_t> out;out.reserve(size_t(r.end-r.begin)*1176);std::array<unsigned char,2352> b{};
 for(uint64_t i=r.begin;i<r.end;++i){f.read(reinterpret_cast<char*>(b.data()),b.size());if(!f)return DataError{"","CD",size_t(i*2352),"short audio sector"};for(size_t j=0;j<b.size();j+=2)out.push_back(static_cast<int16_t>(uint16_t(b[j])|(uint16_t(b[j+1])<<8)));}return out;
}
}
