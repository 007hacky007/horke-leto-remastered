#include "assets.h"
#include "coverage.h"
#include "data/grp.h"
#include "data/adb.h"
#include "script/object.h"
#include "data/flc.h"
#include "data/wav.h"
#include <set>
#include "data/hash.h"
#include <fstream>
#include <iostream>
namespace hl {
Result<nlohmann::json> inventory_grp(const std::filesystem::path& data){auto archive=GrpArchive::open(data/"GFX.GRP");if(!archive.has_value())return archive.error();
 nlohmann::json j{{"schema_version",1},{"used",archive.value().entries().size()},{"capacity",archive.value().capacity()},{"first_payload",archive.value().entries().empty()?0:archive.value().entries()[0].offset},{"flc",0},{"wav",0},{"entries",nlohmann::json::array()},{"bounds_errors",0},{"read_errors",0}};
 for(auto& e:archive.value().entries()){auto b=archive.value().read(e.name);if(!b.has_value())return b.error();auto key=ascii_key(e.name);if(key.ends_with(".flc"))j["flc"]=j["flc"].get<unsigned>()+1;if(key.ends_with(".wav"))j["wav"]=j["wav"].get<unsigned>()+1;
 std::vector<unsigned> metadata;for(auto v:e.metadata)metadata.push_back(std::to_integer<unsigned>(v));j["entries"].push_back({{"name",e.name},{"offset",e.offset},{"size",e.size},{"metadata",metadata},{"sha256",sha256(b.value())}});
 }return j;
}
}
namespace hl {
Result<nlohmann::json> inventory_adb(const std::filesystem::path& data){
 auto db=AdbDatabase::open(data/"DATA.ADB");if(!db.has_value())return db.error();
 nlohmann::json j{{"schema_version",1},{"used",db.value().entries().size()},{"total",db.value().total()},{"max_name",db.value().max_name()},{"payload_base",db.value().payload_base()},{"candidates",0},{"references",0},{"entries",nlohmann::json::array()},{"objects",nlohmann::json::array()}};
 for(auto& e:db.value().entries()){
  auto b=db.value().read(e.name);if(!b.has_value())return b.error();j["entries"].push_back({{"name",e.name},{"size",e.size},{"relative_offset",e.relative_offset},{"sha256",sha256(b.value())}});
  auto x=inspect_object_header(b.value());
  if(!x.has_value())continue;
  j["candidates"]=j["candidates"].get<unsigned>()+1;j["references"]=j["references"].get<unsigned>()+x.value().resources.size();nlohmann::json obj{{"name",e.name},{"code_size",x.value().code_size},{"declared_offset",x.value().declared_table_offset},{"table_offset",x.value().table_offset},{"resources",nlohmann::json::array()}};
  for(auto& r:x.value().resources)obj["resources"].push_back({{"name",r.name},{"flags",r.flags}});j["objects"].push_back(obj);
  if(e.name=="main")j["main"]={{"code_size",x.value().code_size},{"declared_offset",x.value().declared_table_offset},{"table_offset",x.value().table_offset},{"count",x.value().resources.size()}};
 }return j;
}
}
namespace hl {
Result<nlohmann::json> decode_all_flc(const std::filesystem::path& data,const std::filesystem::path& samples){
 auto archive=GrpArchive::open(data/"GFX.GRP");if(!archive.has_value())return archive.error();
 nlohmann::json j{{"schema_version",1},{"files",0},{"visible_frames",0},{"ring_frames",0},{"resources",nlohmann::json::array()},{"errors",nlohmann::json::array()}};
 const std::set<std::string> selected{"A1-PZ!.FLC","C1-VSTAV.FLC","HL!.FLC","IN-PR-MO.FLC","IN-PRE-M.FLC","MAPA7-BL.FLC"};
 auto error=[&](const DataError& e){j["errors"].push_back({{"resource",e.resource},{"offset",e.byte_offset},{"reason",e.reason}});};
 auto dump=[](const std::filesystem::path& path,ByteView b)->Result<void>{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());if(!out)return DataError{"",path.filename().string(),0,"sample write failed"};return {};};
 for(auto& e:archive.value().entries()){
  if(!ascii_key(e.name).ends_with(".flc"))continue;auto b=archive.value().read(e.name);if(!b.has_value())return b.error();auto d=FlcDecoder::open(b.value(),e.name);if(!d.has_value()){error(d.error());continue;}
  nlohmann::json item{{"name",e.name},{"member_size",e.size},{"source_sha256",sha256(b.value())},{"declared_frames",d.value().declared_frames()},{"ring_frames",d.value().ring_frames()},{"frames",nlohmann::json::array()}};
  bool sample=!samples.empty()&&selected.contains(e.name);if(sample){std::filesystem::create_directories(samples/e.name);auto r=dump(samples/e.name/"source.flc",b.value());if(!r.has_value())return r.error();}
  for(;;){auto r=d.value().next();if(!r.has_value()){error(r.error());break;}if(!r.value())break;auto& f=*r.value();item["width"]=f.logical_width;item["height"]=f.logical_height;
   item["frames"].push_back({{"index",f.frame_index},{"indices_sha256",sha256(std::as_bytes(std::span(f.indices)))},{"palette_sha256",sha256(std::as_bytes(std::span(f.palette)))},{"declared_delay",{{"numerator",f.declared_delay.numerator},{"denominator",f.declared_delay.denominator}}},{"effective_delay",{{"numerator",f.effective_delay.numerator},{"denominator",f.effective_delay.denominator},{"unit","engine_updates"}}}});
   if(sample&&(f.frame_index==0||f.frame_index==d.value().declared_frames()/2||f.frame_index+1==d.value().declared_frames())){
    auto prefix=samples/e.name/("frame-"+std::to_string(f.frame_index));auto out=dump(prefix.string()+".indices",std::as_bytes(std::span(f.indices)));if(!out.has_value())return out.error();out=dump(prefix.string()+".palette",std::as_bytes(std::span(f.palette)));if(!out.has_value())return out.error();std::vector<uint8_t> rgb;rgb.reserve(f.indices.size()*3);for(auto v:f.indices)for(auto c:f.palette[v])rgb.push_back(c);out=dump(prefix.string()+".rgb",std::as_bytes(std::span(rgb)));if(!out.has_value())return out.error();
   }
  }
  j["files"]=j["files"].get<unsigned>()+1;j["visible_frames"]=j["visible_frames"].get<unsigned>()+item["frames"].size();j["ring_frames"]=j["ring_frames"].get<unsigned>()+d.value().ring_frames();j["resources"].push_back(std::move(item));
 }return j;
}
}
namespace hl {
Result<nlohmann::json> decode_all_wav(const std::filesystem::path& data){
 auto a=GrpArchive::open(data/"GFX.GRP");if(!a.has_value())return a.error();nlohmann::json j{{"schema_version",1},{"files",0},{"resources",nlohmann::json::array()}};
 for(auto& e:a.value().entries())if(ascii_key(e.name).ends_with(".wav")){auto b=a.value().read(e.name);if(!b.has_value())return b.error();auto pcm=decode_wav(b.value());if(!pcm.has_value()){auto error=pcm.error();error.resource=e.name;return error;}auto& p=pcm.value();j["resources"].push_back({{"name",e.name},{"rate",p.rate},{"channels",p.channels},{"sample_frames",p.frames},{"sample_format",p.sample_format},{"channel_order",p.channel_order},{"source_sha256",sha256(b.value())},{"pcm_sha256",sha256(std::as_bytes(std::span(p.samples)))},{"duration",{{"numerator",p.frames},{"denominator",p.rate}}}});j["files"]=j["files"].get<unsigned>()+1;}
 return j;
}
}
int assets_command(int argc,char** argv){
 if(argc<2)return 2;std::map<std::string,std::string> args;
 for(int i=2;i<argc;++i){std::string key=argv[i];if(key=="--all"){args[key]="true";continue;}if(i+1>=argc||!args.emplace(key,argv[i+1]).second)return 2;++i;}
 if(!args.contains("--data")||!args.contains("--report"))return 2;std::string command=argv[1];
 hl::Result<nlohmann::json> r=hl::DataError{"","assets",0,"unknown assets subcommand"};
 if(command=="verify"&&args.contains("--frames"))r=hl::verify_assets(args["--data"],args["--frames"]);
 else if(command=="inventory")r=hl::inventory_grp(args["--data"]);
 else if(command=="adb")r=hl::inventory_adb(args["--data"]);
 else if(command=="decode-wav"&&args.contains("--all"))r=hl::decode_all_wav(args["--data"]);
 else if(command=="export-font"&&args.contains("--out"))r=hl::export_font(args["--data"],args["--out"]);
 else if(command=="export-frames"&&args.contains("--out"))r=hl::export_all_flc(args["--data"],args["--out"]);
 else if(command=="decode-flc"&&args.contains("--all"))r=hl::decode_all_flc(args["--data"],args["--samples"]);
 if(!r.has_value()){std::cerr<<r.error().resource<<":"<<r.error().byte_offset<<": "<<r.error().reason<<'\n';return 1;}
 std::ofstream out(args["--report"]);out<<r.value().dump(2)<<'\n';return out&&(!r.value().contains("errors")||r.value()["errors"].empty())?0:1;
}
namespace hl {
nlohmann::json opcode_coverage(const ScriptObject& o,const nlohmann::json& trace) {
 nlohmann::json report{{"object",o.name},{"opcodes",nlohmann::json::object()},{"handlers",nlohmann::json::array()}};
 std::set<std::pair<size_t,unsigned>> reached;for(const auto& t:trace)if(t.value("object",std::string{})==o.name)reached.emplace(t.value("offset",size_t(-1)),t.value("opcode",unsigned(-1)));
 for(const auto& ins:o.instructions){auto key=std::to_string(ins.opcode);auto& row=report["opcodes"][key];
  if(row.is_null())row={{"runtime_bound",native_opcode_bound(ins.opcode)},{"offsets",nlohmann::json::array()},{"executed_offsets",nlohmann::json::array()}};
  row["offsets"].push_back(ins.offset);
  if(reached.contains({ins.offset,ins.opcode}))row["executed_offsets"].push_back(ins.offset);
 }
 report["initialization_reached"]=reached.contains({0,0});
 for(const auto& h:o.handlers){auto ins=std::find_if(o.instructions.begin(),o.instructions.end(),[&](const auto& ins){return ins.offset==h.entry;});report["handlers"].push_back({{"declaration",h.declaration},{"entry",h.entry},{"end",h.end},{"entry_reached",ins!=o.instructions.end()&&reached.contains({h.entry,ins->opcode})}});}
 return report;
}
nlohmann::json catalog_service_calls(const ScriptObject& o) {
 nlohmann::json calls=nlohmann::json::array();
 for(size_t i=0;i<o.instructions.size();++i){const auto& ins=o.instructions[i];if(ins.opcode<54)continue;
  nlohmann::json row{{"object",o.name},{"offset",ins.offset},{"opcode",ins.opcode},{"handler_address",dos_opcode_map()[ins.opcode].handler_address},{"literal_top",nullptr},{"literal_resource",nullptr}};
  if(i&&o.instructions[i-1].next_offset==ins.offset&&o.instructions[i-1].opcode>=9&&o.instructions[i-1].opcode<=13) {
   auto value=o.instructions[i-1].operand;row["literal_top"]=value;
   if((ins.opcode==97||ins.opcode==98||ins.opcode==112||ins.opcode==113)&&value>=0&&size_t(value)<o.layout.resources.size())row["literal_resource"]=o.layout.resources[size_t(value)].name;
  }calls.push_back(std::move(row));
 }return calls;
}
Result<nlohmann::json> verify_scripts(const std::filesystem::path& data,const nlohmann::json& trace) {
 auto db=AdbDatabase::open(data/"DATA.ADB");if(!db.has_value())return db.error();
 std::map<std::string,nlohmann::json> traces;for(const auto& t:trace){auto key=t.value("object",std::string{});if(!traces.contains(key))traces[key]=nlohmann::json::array();traces[key].push_back(t);}
 nlohmann::json report{{"schema_version",1},{"objects",0},{"decoded_objects",0},{"unknown_instructions",0},{"instructions",0},{"handlers",0},{"scripts",nlohmann::json::array()},{"errors",nlohmann::json::array()},{"opcode_counts",nlohmann::json::object()}};
 for(const auto& entry:db.value().entries()) {
  auto b=db.value().read(entry.name);if(!b.has_value())return b.error();auto bytes=b.value();
  if(bytes.size()<7||bytes[2]!=std::byte{0}||bytes[3]!=std::byte{0}||bytes[6]!=std::byte{1})continue;
  report["objects"]=report["objects"].get<size_t>()+1;
  auto object=load_object(bytes,entry.name);
  if(!object.has_value()) {
   const auto& e=object.error();report["errors"].push_back({{"object",e.resource},{"offset",e.byte_offset},{"reason",e.reason}});
   if(e.reason.starts_with("unknown opcode"))report["unknown_instructions"]=report["unknown_instructions"].get<size_t>()+1;
   continue;
  }
  const auto& o=object.value();
  report["opcode_coverage"].push_back(opcode_coverage(o,traces.contains(o.name)?traces.at(o.name):nlohmann::json::array()));
  for(auto& call:catalog_service_calls(o))report["service_calls"].push_back(std::move(call));
  report["decoded_objects"]=report["decoded_objects"].get<size_t>()+1;
  report["instructions"]=report["instructions"].get<size_t>()+o.instructions.size();report["handlers"]=report["handlers"].get<size_t>()+o.handlers.size();
  nlohmann::json handlers=nlohmann::json::array();for(const auto& h:o.handlers)handlers.push_back({{"declaration",h.declaration},{"entry",h.entry},{"end",h.end}});
  for(const auto& ins:o.instructions) {
   auto key=std::to_string(ins.opcode);auto& counts=report["opcode_counts"];
   counts[key]=counts.value(key,size_t(0))+1;
  }
  report["scripts"].push_back({{"name",entry.name},{"source_sha256",sha256(bytes)},{"code_size",o.code.size()},{"executable_end",o.executable_end},{"table_offset",o.layout.table_offset},{"instructions",o.instructions.size()},{"handlers",handlers}});
 }
 return report;
}
}
int scripts_command(int argc,char** argv) {
 if((argc!=6&&argc!=8)||std::string_view(argv[1])!="verify")return 2;
 std::map<std::string,std::string> args;
 for(int i=2;i<argc;i+=2)if(!args.emplace(argv[i],argv[i+1]).second)return 2;
 if(!args.contains("--data")||!args.contains("--report"))return 2;
 nlohmann::json trace=nlohmann::json::array();if(args.contains("--trace")){std::ifstream input(args["--trace"]);if(!input)return 1;trace=nlohmann::json::parse(input,nullptr,false);if(!trace.is_array())return 1;}
 auto result=hl::verify_scripts(args["--data"],trace);
 if(!result.has_value()){const auto& e=result.error();std::cerr<<e.resource<<":"<<e.byte_offset<<": "<<e.reason<<'\n';return 1;}
 const auto& j=result.value();std::ofstream out(args["--report"]);out<<j.dump(2)<<'\n';
 return out&&j["objects"]==833&&j["decoded_objects"]==833&&j["errors"].empty()?0:1;
}
