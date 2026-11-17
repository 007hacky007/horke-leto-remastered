#include "data/grp.h"
#include "data/byte_reader.h"
#include <fstream>
#include <algorithm>
namespace hl {
std::string ascii_key(std::string_view name){std::string key(name);for(char& c:key)if(c>='A'&&c<='Z')c+=32;return key;}
Result<std::string> archive_name(ByteView b,bool required){std::string name;bool terminated=false;for(auto x:b){auto c=std::to_integer<unsigned char>(x);if(!c){terminated=true;break;}if(c<32||c>126||c=='/'||c=='\\'||c==':')return DataError{"","directory",0,"invalid member name"};name+=char(c);}if(name.empty()||name=="."||name==".."||(required&&!terminated))return DataError{"","directory",0,"empty or unterminated member name"};return name;}
Result<std::vector<std::byte>> read_file(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return DataError{path.string(),"",0,"cannot open file"};auto size=f.tellg();if(size<0)return DataError{path.string(),"",0,"cannot determine size"};std::vector<std::byte>b(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),b.size());if(!f)return DataError{path.string(),"",0,"short file read"};return b;}
Result<GrpArchive> GrpArchive::open(const std::filesystem::path& p){auto b=read_file(p);if(!b.has_value())return b.error();auto r=parse(std::move(b.value()));if(!r.has_value()){auto e=r.error();e.file=p.filename().string();return e;}return r;}
Result<GrpArchive> GrpArchive::parse(std::vector<std::byte> bytes){
 auto fail=[](size_t p,std::string why)->Result<GrpArchive>{return DataError{"","GRP",p,std::move(why)};};ByteReader r(bytes);auto signature=r.take(16);if(!signature.has_value()||std::string(reinterpret_cast<const char*>(signature.value().data()),16)!=std::string("AGDS group file\x1a",16))return fail(0,"invalid GRP signature");
 auto h=r.u32le(),magic=r.u32le(),version=r.u32le(),used=r.u32le(),capacity=r.u32le();if(!capacity.has_value()||!r.take(8).has_value())return fail(16,"truncated GRP header");
 if(h.value()!=44||magic.value()!=0x1a03c9e6||version.value()!=1)return fail(16,"unsupported GRP header/version");if(used.value()>capacity.value()||uint64_t(capacity.value())*30>bytes.size()-44)return fail(28,"invalid allocated GRP directory");
 auto payload_base=44+uint64_t(capacity.value())*30;GrpArchive out;out.capacity_=capacity.value();
 for(uint32_t i=0;i<used.value();++i){auto pos=r.position();auto raw=r.take(14);auto name=archive_name(raw.value(),false);if(!name.has_value())return name.error();auto offset=r.u32le().value(),size=r.u32le().value();auto metadata=r.take(8).value();if(offset<payload_base||!checked_range(bytes.size(),offset,size))return fail(pos+14,"GRP payload outside file");if(!out.lookup_.emplace(ascii_key(name.value()),out.entries_.size()).second)return fail(pos,"colliding GRP member names");GrpEntry e{name.value(),offset,size,{}};std::copy(metadata.begin(),metadata.end(),e.metadata.begin());out.entries_.push_back(std::move(e));
 }out.bytes_=std::move(bytes);return out;
}
Result<std::vector<std::byte>> GrpArchive::read(std::string_view name)const{auto i=lookup_.find(ascii_key(name));if(i==lookup_.end())return DataError{"",std::string(name),0,"GRP member missing"};auto& e=entries_[i->second];return std::vector<std::byte>(bytes_.begin()+e.offset,bytes_.begin()+e.offset+e.size);}
}
