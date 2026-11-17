#include "save/save.h"
#include "data/hash.h"
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
constexpr uint32_t max_size=16*1024*1024;
}
Result<void> save_game(const SaveState& state,const std::filesystem::path& path) {
 std::string payload=nlohmann::json{{"data_identity",state.data_identity},{"reconstruction",state.reconstruction}}.dump();if(payload.size()>max_size)return error("save exceeds bounded format size");
 auto checksum=sha256(std::as_bytes(std::span(payload)));auto staging=path;staging+=".tmp";
 std::ofstream out(staging,std::ios::binary|std::ios::trunc);if(!out)return error("cannot open staging save");
 out.write("HLSV",4);auto word=[&](uint32_t v){for(unsigned i=0;i<4;++i)out.put(char(v>>(i*8)));};word(1);word(uint32_t(payload.size()));out.write(checksum.data(),64);out.write(payload.data(),std::streamsize(payload.size()));out.flush();bool success=bool(out);out.close();if(!success||!out){std::error_code ec;std::filesystem::remove(staging,ec);return error("staging save write failed");}
#ifdef _WIN32
 if(!MoveFileExW(staging.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;std::filesystem::remove(staging,ec);return error("atomic save replacement failed");}
#else
 std::error_code ec;std::filesystem::rename(staging,path,ec);if(ec){std::filesystem::remove(staging,ec);return error("atomic save replacement failed");}
#endif
 return {};
}
Result<SaveState> load_game(const std::filesystem::path& path,std::string_view identity) {
 std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size<76||size>max_size+76)return error("missing or invalid save size");auto b=read_file(path);if(!b.has_value())return error("cannot read save");ByteReader r(b.value());auto magic=r.take(4).value();if(std::string(reinterpret_cast<const char*>(magic.data()),4)!="HLSV")return error("invalid save signature");
 if(r.u32le().value()!=1)return error("unsupported save version");auto length=r.u32le().value();if(length!=r.remaining()-64)return error("truncated or trailing save payload");auto digest=r.take(64).value(),payload=r.take(length).value();if(sha256(payload)!=std::string(reinterpret_cast<const char*>(digest.data()),64))return error("save checksum mismatch");
 try {auto j=nlohmann::json::parse(reinterpret_cast<const char*>(payload.data()),reinterpret_cast<const char*>(payload.data())+payload.size());if(j.at("data_identity").get<std::string>()!=identity)return error("save data identity mismatch");if(!j.at("reconstruction").is_object())return error("invalid reconstruction state");return SaveState{std::string(identity),j.at("reconstruction")};}
 catch(const nlohmann::json::exception&){return error("invalid save payload");}
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
 for(auto key:{"text_speed","music_volume","sound_volume","video_speed","mouse_speed","text_mode","version"}){auto value=config.u32le();if(!value.has_value())return fail("truncated DOS system integers");out.integers[key]=std::bit_cast<int32_t>(value.value());}if(config.remaining())return fail("unexpected DOS system settings tail");
 for(unsigned i=0;i<35;++i){auto item=inv.value().subspan(size_t(i)*40,40);auto count=word(item,32);if(!count)continue;if(count>10000)return fail("DOS inventory count exceeds bound");auto n=name(item.first(32));if(!n.has_value()||n.value().empty())return fail("invalid DOS inventory item");for(unsigned j=0;j<i;++j)if(out.inventory[j].item==n.value())return fail("duplicate DOS inventory item");out.inventory[i]={n.value(),count};}
 for(const auto& entry:archive.value().entries()){
  if(entry.name.starts_with("__")){if(entry.name!="__c"&&entry.name!="__s"&&entry.name!="__v"&&entry.name!="__d"&&entry.name!="__i")return fail("unknown DOS save section");continue;}
  auto payload=archive.value().read(entry.name).value();
  if(payload.size()==66){auto n=name(payload.first(32));if(!n.has_value())return n.error();if(!n.value().empty())out.regions[entry.name]=n.value();continue;}
  if(payload.size()<889||std::to_integer<unsigned>(payload[0])>1)return fail("invalid DOS scene header");
  auto n=name(payload.subspan(1,32));if(!n.has_value())return n.error();auto count=word(payload,85);if(count>1000||payload.size()!=889+size_t(count)*34)return fail("invalid DOS scene member table");
  DosSceneState saved{payload[0]==std::byte{1},n.value(),{}};
  for(uint32_t i=0;i<count;++i){auto member=payload.subspan(889+size_t(i)*34,34);auto n=name(member.subspan(2,32));if(!n.has_value()||n.value().empty()||!saved.members.emplace(n.value(),short_word(member,0)).second)return fail("invalid or duplicate DOS scene member");}
  out.scenes.emplace(entry.name,std::move(saved));
 }
 if(!out.scenes.contains(out.scene)||!out.scenes.at(out.scene).visited)return fail("current DOS scene is missing or unvisited");return out;
}
}
