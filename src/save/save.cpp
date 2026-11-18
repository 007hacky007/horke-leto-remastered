#include "save/save.h"
#include "data/grp.h"
#include "data/byte_reader.h"
#include "data/adb.h"
#include <bit>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace hl {
namespace {
DataError error(std::string reason){return {"","save",0,std::move(reason)};}
}
Result<void> write_file_atomic(const std::filesystem::path& path,std::span<const std::byte> bytes) {
 auto staging=path;staging+=".tmp";
 std::ofstream out(staging,std::ios::binary|std::ios::trunc);if(!out)return error("cannot open staging save");
 out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.flush();bool success=bool(out);out.close();if(!success||!out){std::error_code ec;std::filesystem::remove(staging,ec);return error("staging save write failed");}
#ifdef _WIN32
 if(!MoveFileExW(staging.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;std::filesystem::remove(staging,ec);return error("atomic save replacement failed");}
#else
 std::error_code ec;std::filesystem::rename(staging,path,ec);if(ec){std::filesystem::remove(staging,ec);return error("atomic save replacement failed");}
#endif
 return {};
}
std::filesystem::path dos_save_path(const std::filesystem::path& directory,int slot){return directory/("SAVE."+std::to_string(slot));}
std::array<std::vector<uint8_t>,10> read_save_names(const std::filesystem::path& directory) {
 std::array<std::vector<uint8_t>,10> names;auto bytes=read_file(directory/"SAVNAMES.TXT");if(!bytes.has_value()||bytes.value().size()!=510)return names;
 for(size_t slot=0;slot<10;++slot)for(size_t i=0;i<50;++i){auto c=std::to_integer<uint8_t>(bytes.value()[slot*51+i]);if(!c)break;names[slot].push_back(c);}
 return names;
}
Result<void> write_save_names(const std::filesystem::path& directory,const std::array<std::vector<uint8_t>,10>& names) {
 std::vector<std::byte> bytes(510);for(size_t slot=0;slot<10;++slot){if(names[slot].size()>50)return error("save name exceeds 50 glyphs");for(size_t i=0;i<names[slot].size();++i)bytes[slot*51+i]=std::byte(names[slot][i]);}
 return write_file_atomic(directory/"SAVNAMES.TXT",bytes);
}
}

namespace hl {
Result<DosSaveState> parse_dos_save(ByteView bytes) {
 // DOS 0x2cd39 loads these five ADB entries; 0x2c19a writes 0x379-byte scene headers.
 auto fail=[](std::string why)->Result<DosSaveState>{return DataError{"","DOS save",0,std::move(why)};};
 if(bytes.size()>1024*1024)return fail("DOS save exceeds bounded size");
 auto archive=AdbDatabase::parse({bytes.begin(),bytes.end()});if(!archive.has_value())return archive.error();
 auto name=[](ByteView field)->Result<std::string>{
  auto end=std::find(field.begin(),field.end(),std::byte{});if(end==field.end())return DataError{"","DOS save",0,"unterminated name"};
  for(auto i=field.begin();i!=end;++i)if(std::to_integer<unsigned>(*i)<32||std::to_integer<unsigned>(*i)>126||*i==std::byte{'/'}||*i==std::byte{'\\'})return DataError{"","DOS save",0,"invalid name"};
  return std::string(reinterpret_cast<const char*>(field.data()),size_t(end-field.begin()));
 };
 auto word=[](ByteView b,size_t off){return uint32_t(std::to_integer<uint8_t>(b[off]))|(uint32_t(std::to_integer<uint8_t>(b[off+1]))<<8)|(uint32_t(std::to_integer<uint8_t>(b[off+2]))<<16)|(uint32_t(std::to_integer<uint8_t>(b[off+3]))<<24);};
 auto short_word=[](ByteView b,size_t off){return int16_t(uint16_t(std::to_integer<uint8_t>(b[off]))|(uint16_t(std::to_integer<uint8_t>(b[off+1]))<<8));};
 auto c=archive.value().read("__c"),s=archive.value().read("__s"),v=archive.value().read("__v"),d=archive.value().read("__d"),inv=archive.value().read("__i");
 if(!c.has_value()||!s.has_value()||!v.has_value()||!d.has_value()||!inv.has_value())return fail("missing DOS save section");
 if(c.value().size()!=42||s.value().size()!=800||inv.value().size()!=1400||v.value().size()<4)return fail("invalid DOS save section size");
 DosSaveState out;auto scene=name(s.value().first(32)),chr=name(c.value().first(16));if(!scene.has_value()||!chr.has_value()||scene.value().empty())return fail("invalid scene or character name");out.scene=scene.value();out.character_file=chr.value();
 for(size_t i=0;i<256;++i)for(size_t component=0;component<3;++component)out.palette[i][component]=std::to_integer<uint8_t>(s.value()[32+i*3+component]);
 if(out.character_file!="MAJER.CHR"&&out.character_file!="JOKES.CHR")return fail("unsupported DOS character file");
 out.x=short_word(c.value(),32);out.y=short_word(c.value(),34);out.direction=short_word(c.value(),36);out.movement_depth=short_word(c.value(),40);auto visible=short_word(c.value(),38);
 if(out.direction<0||out.direction>7||out.movement_depth<0||visible<0||visible>1)return fail("invalid DOS character state");out.character_visible=visible==1;
 uint32_t count=word(v.value(),0);if(count>1000||v.value().size()!=4+size_t(count)*36)return fail("invalid DOS global table");
 for(uint32_t i=0;i<count;++i){auto n=name(v.value().subspan(4+size_t(i)*36,32));if(!n.has_value()||n.value().empty()||!out.globals.emplace(n.value(),std::bit_cast<int32_t>(word(v.value(),36+size_t(i)*36))).second)return fail("invalid or duplicate DOS global");}
 ByteReader config(d.value());for(auto key:{"inventory_scr","escape_scr","load_scr","save_scr"}){auto len=config.u8();if(!len.has_value()||!len.value()||len.value()>32)return fail("invalid DOS system string length");auto field=config.take(len.value());if(!field.has_value())return fail("truncated DOS system string");auto n=name(field.value());if(!n.has_value())return n.error();out.strings[key]=n.value();}
 for(auto key:{"gfx_bright","gfx_contrast","sound_volume","music_volume","text_speed","tell_mode","version"}){auto value=config.u32le();if(!value.has_value())return fail("truncated DOS system integers");out.integers[key]=std::bit_cast<int32_t>(value.value());}if(config.remaining())return fail("unexpected DOS system settings tail");
 for(unsigned i=0;i<35;++i){auto item=inv.value().subspan(size_t(i)*40,40);auto count=word(item,32);if(!count)continue;if(count>10000)return fail("DOS inventory count exceeds bound");auto n=name(item.first(32));if(!n.has_value()||n.value().empty())return fail("invalid DOS inventory item");for(unsigned j=0;j<i;++j)if(out.inventory[j].item==n.value())return fail("duplicate DOS inventory item");out.inventory[i]={n.value(),count};}
 for(const auto& entry:archive.value().entries()){
  if(entry.name.starts_with("__")){if(entry.name!="__c"&&entry.name!="__s"&&entry.name!="__v"&&entry.name!="__d"&&entry.name!="__i")return fail("unknown DOS save section");continue;}
  auto payload=archive.value().read(entry.name).value();
  if(payload.size()==66){auto n=name(payload.first(32));if(!n.has_value())return n.error();if(!n.value().empty())out.regions[entry.name]=n.value();continue;}
  if(payload.size()<889||std::to_integer<unsigned>(payload[0])>1)return fail("invalid DOS scene header");
  auto n=name(payload.subspan(1,32));if(!n.has_value())return n.error();auto count=word(payload,85);if(count>1000||payload.size()!=889+size_t(count)*34)return fail("invalid DOS scene member table");
  DosSceneState saved{payload[0]==std::byte{1},n.value(),{}};
  // The original leaves stale memory in unused name fields, so these two are informative only.
  auto loose=[&](ByteView field){auto v=name(field);return v.has_value()?v.value():std::string{};};
  saved.entered_from=loose(payload.subspan(33,32));saved.cursor=loose(payload.subspan(857,32));
  saved.screen=std::bit_cast<int32_t>(word(payload,65));saved.x=std::bit_cast<int32_t>(word(payload,69));saved.y=std::bit_cast<int32_t>(word(payload,73));saved.direction=std::bit_cast<int32_t>(word(payload,77));saved.actor_visible=std::bit_cast<int32_t>(word(payload,81));
  for(size_t i=0;i<256;++i)for(size_t component=0;component<3;++component)saved.palette[i][component]=std::to_integer<uint8_t>(payload[89+i*3+component]);
  for(uint32_t i=0;i<count;++i){auto member=payload.subspan(889+size_t(i)*34,34);auto n=name(member.subspan(2,32));if(!n.has_value()||n.value().empty()||!saved.members.emplace(n.value(),short_word(member,0)).second)return fail("invalid or duplicate DOS scene member");}
  out.scenes.emplace(entry.name,std::move(saved));
 }
 if(!out.scenes.contains(out.scene)||!out.scenes.at(out.scene).visited)return fail("current DOS scene is missing or unvisited");return out;
}

Result<std::vector<std::byte>> write_dos_save(const DosSaveState& state) {
 auto fail=[](std::string why)->Result<std::vector<std::byte>>{return DataError{"","DOS save",0,std::move(why)};};
 auto valid=[](const std::string& n,size_t width){if(n.size()>=width)return false;for(unsigned char c:n)if(c<32||c>126||c=='/'||c=='\\')return false;return true;};
 using Bytes=std::vector<std::byte>;
 auto put_name=[](Bytes& b,size_t off,const std::string& n){for(size_t i=0;i<n.size();++i)b[off+i]=std::byte(n[i]);};
 auto put32=[](Bytes& b,size_t off,uint32_t v){for(unsigned i=0;i<4;++i)b[off+i]=std::byte(uint8_t(v>>(i*8)));};
 auto put16=[](Bytes& b,size_t off,int v){auto u=uint16_t(int16_t(v));b[off]=std::byte(uint8_t(u));b[off+1]=std::byte(uint8_t(u>>8));};
 auto put_palette=[](Bytes& b,size_t off,const std::array<std::array<uint8_t,3>,256>& palette){for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c)b[off+i*3+c]=std::byte(palette[i][c]);};
 std::map<std::string,Bytes> entries;
 if(!valid(state.character_file,16))return fail("invalid character file");
 Bytes c(42);put_name(c,0,state.character_file);put16(c,32,state.x);put16(c,34,state.y);put16(c,36,state.direction);put16(c,38,state.character_visible?1:0);put16(c,40,state.movement_depth);entries["__c"]=std::move(c);
 if(!valid(state.scene,32)||!state.scenes.contains(state.scene))return fail("invalid current scene");
 Bytes sc(800);put_name(sc,0,state.scene);put_palette(sc,32,state.palette);entries["__s"]=std::move(sc);
 Bytes v(4+state.globals.size()*36);put32(v,0,uint32_t(state.globals.size()));size_t i=0;
 for(const auto& [n,value]:state.globals){if(!valid(n,32))return fail("invalid global name");put_name(v,4+i*36,n);put32(v,36+i*36,std::bit_cast<uint32_t>(value));++i;}entries["__v"]=std::move(v);
 Bytes d;for(auto key:{"inventory_scr","escape_scr","load_scr","save_scr"}){auto it=state.strings.find(key);std::string value=it==state.strings.end()?std::string{}:it->second;if(!valid(value,32))return fail(std::string("invalid system string ")+key);d.push_back(std::byte(uint8_t(value.size()+1)));for(char ch:value)d.push_back(std::byte(ch));d.push_back(std::byte{});}
 for(auto key:{"gfx_bright","gfx_contrast","sound_volume","music_volume","text_speed","tell_mode","version"}){auto it=state.integers.find(key);if(it==state.integers.end())return fail(std::string("missing system integer ")+key);auto off=d.size();d.resize(off+4);put32(d,off,std::bit_cast<uint32_t>(it->second));}entries["__d"]=std::move(d);
 // HL.EXE stores a live object pointer after each count; a zero there hides the item in the inventory screen.
 Bytes inv(1400);for(size_t slot=0;slot<35;++slot){const auto& item=state.inventory[slot];if(!item.count)continue;if(!valid(item.item,32)||item.item.empty())return fail("invalid inventory item");put_name(inv,slot*40,item.item);put32(inv,slot*40+32,item.count);put32(inv,slot*40+36,0xffffffffu);}entries["__i"]=std::move(inv);
 for(const auto& [id,scene]:state.scenes){
  if(!valid(id,32)||!valid(scene.region,32))return fail("invalid scene record");
  Bytes b(889+scene.members.size()*34);b[0]=std::byte(scene.visited?1:0);put_name(b,1,scene.region);if(valid(scene.entered_from,32))put_name(b,33,scene.entered_from);
  put32(b,65,std::bit_cast<uint32_t>(scene.screen));put32(b,69,std::bit_cast<uint32_t>(scene.x));put32(b,73,std::bit_cast<uint32_t>(scene.y));put32(b,77,std::bit_cast<uint32_t>(scene.direction));put32(b,81,std::bit_cast<uint32_t>(scene.actor_visible));put32(b,85,uint32_t(scene.members.size()));
  put_palette(b,89,scene.palette);if(valid(scene.cursor,32))put_name(b,857,scene.cursor);
  size_t m=0;for(const auto& [member,count]:scene.members){if(!valid(member,32)||member.empty())return fail("invalid scene member");put16(b,889+m*34,count);put_name(b,891+m*34,member);++m;}
  entries[id]=std::move(b);
 }
 for(const auto& [id,region]:state.regions){if(state.scenes.contains(id))continue;if(!valid(id,32)||!valid(region,32))return fail("invalid region override");Bytes b(66);put_name(b,0,region);entries[id]=std::move(b);}
 // ADB container as HL.EXE writes it: three spare directory slots, payloads in directory order.
 uint32_t used=uint32_t(entries.size()),total=used+3;size_t base=20+size_t(total)*40,payload=0;for(const auto& [n,b]:entries)payload+=b.size();
 Bytes out(base+payload);put32(out,0,666);put32(out,4,1);put32(out,8,total);put32(out,12,used);put32(out,16,31);
 size_t slot=0,offset=0;for(const auto& [n,b]:entries){put32(out,20+slot*40,uint32_t(offset));put_name(out,24+slot*40,n);put32(out,56+slot*40,uint32_t(b.size()));std::copy(b.begin(),b.end(),out.begin()+std::ptrdiff_t(base+offset));offset+=b.size();++slot;}
 if(out.size()>1024*1024)return fail("DOS save exceeds bounded size");
 return out;
}
}
