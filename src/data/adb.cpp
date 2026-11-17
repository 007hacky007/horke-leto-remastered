#include "data/adb.h"
#include "data/byte_reader.h"
#include "data/grp.h"
namespace hl {
Result<AdbDatabase> AdbDatabase::open(const std::filesystem::path& p){auto b=read_file(p);if(!b.has_value())return b.error();return parse(std::move(b.value()));}
Result<AdbDatabase> AdbDatabase::parse(std::vector<std::byte> bytes){
 auto fail=[](size_t p,std::string why)->Result<AdbDatabase>{return DataError{"","ADB",p,std::move(why)};};ByteReader r(bytes);auto magic=r.u32le(),writable=r.u32le(),total=r.u32le(),used=r.u32le(),max_name=r.u32le();if(!max_name.has_value())return fail(0,"truncated ADB header");
 if(magic.value()!=666||writable.value()>1||used.value()>total.value()||max_name.value()!=31||uint64_t(total.value())*40>bytes.size()-20)return fail(0,"invalid ADB header/directory");
 AdbDatabase out;out.base_=20+size_t(total.value())*40;out.total_=total.value();out.max_name_=max_name.value();
 for(uint32_t i=0;i<used.value();++i){auto p=r.position();auto off=r.u32le().value();auto name=archive_name(r.take(32).value(),true);auto size=r.u32le().value();if(!name.has_value())return name.error();if(!checked_range(bytes.size()-out.base_,off,size))return fail(p,"ADB payload outside file");if(!out.lookup_.emplace(ascii_key(name.value()),out.entries_.size()).second)return fail(p,"duplicate ADB name");out.entries_.push_back({name.value(),off,size});}
 out.bytes_=std::move(bytes);return out;
}
Result<ByteView> AdbDatabase::read(std::string_view name)const{auto i=lookup_.find(ascii_key(name));if(i==lookup_.end())return DataError{"",std::string(name),0,"ADB entry missing"};auto& e=entries_[i->second];return ByteView(bytes_).subspan(base_+e.relative_offset,e.size);}
Result<ObjectLayout> inspect_object_header(ByteView bytes){
 auto fail=[](size_t p,std::string why)->Result<ObjectLayout>{return DataError{"","object",p,std::move(why)};};ByteReader r(bytes);auto id=r.u16le(),data_size=r.u16le(),code_size=r.u16le();auto flags=r.u8();if(!id.has_value()||!data_size.has_value()||!code_size.has_value()||!flags.has_value())return fail(0,"truncated seven-byte object header");if(data_size.value()!=0||flags.value()!=1||size_t(code_size.value())+7!=bytes.size())return fail(0,"object header/size mismatch");
 auto code=bytes.subspan(7);ByteReader c(code);auto enter=c.u8();auto marker=c.u16le(),size=c.u16le();if(!size.has_value()||enter.value()!=0||marker.value()!=0xdead||size.value()!=12||code.size()<17)return fail(7,"invalid Enter structural header");
 ByteReader fields(code.subspan(11));auto declared=fields.u16le().value(),count=fields.u16le().value();size_t table=size_t(declared)+22;if(table>code.size()||size_t(count)*4>code.size()-table)return fail(table+7,"resource table outside object");
 ObjectLayout out;out.id=id.value();out.code_size=code_size.value();out.flags=flags.value();out.table_offset=table;out.declared_table_offset=declared;out.code_end=table;
 ByteReader records(code.subspan(table,size_t(count)*4));
 for(unsigned i=0;i<count;++i){auto off=records.u16le().value(),fl=records.u16le().value();if(off<size_t(count)*4||off>=code.size()-table)return fail(table+7+i*4,"resource name outside table strings");auto name=code.subspan(table+off);size_t n=0;while(n<name.size()&&name[n]!=std::byte{})++n;if(n==name.size())return fail(table+off+7,"unterminated resource name");out.resources.push_back({std::string(reinterpret_cast<const char*>(name.data()),n),fl});}
 return out;
}
}
