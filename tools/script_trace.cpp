#include "script_trace.h"
#include "script/interpreter.h"
#include <fstream>
#include <iostream>
#include <map>
namespace hl {
namespace {
class RejectServices:public EngineServices {
 Result<StepOutcome> invoke(Process& p,const Instruction& ins)override{return DataError{"",p.object->name,ins.offset,"service not part of core comparison"};}
};
}
Result<nlohmann::json> compare_dispatch_trace(const std::filesystem::path& path,const std::filesystem::path& data) {
 std::ifstream input(path);if(!input)return DataError{"","dispatch trace",0,"reference trace missing"};
 std::vector<nlohmann::json> rows;
 try {for(std::string line;std::getline(input,line);)if(!line.empty())rows.push_back(nlohmann::json::parse(line));}
 catch(const nlohmann::json::exception&){return DataError{"","dispatch trace",rows.size(),"invalid trace JSON"};}
 auto db=AdbDatabase::open(data/"DATA.ADB");if(!db.has_value())return db.error();
 std::map<std::string,ScriptObject> objects;
 nlohmann::json j{{"schema_version",1},{"samples",rows.size()},{"decoded_samples",0},{"compared_core_transitions",0},{"mismatches",nlohmann::json::array()},{"uncompared",nlohmann::json::array()},{"full_interpreter_verified",false},{"compared_fields",{"object","opcode","offset","next_pc","stack_depth","stack_values"}},{"unavailable_fields",{"native rendering/audio/world effects","reference globals before trace start","scheduler timing between sampled dispatches"}}};
 try {
  for(size_t i=0;i<rows.size();++i) {
   const auto& row=rows[i];auto name=row.at("object").get<std::string>();
   if(!objects.contains(name)){auto b=db.value().read(name);if(!b.has_value())return b.error();auto o=load_object(b.value(),name);if(!o.has_value())return o.error();objects.emplace(name,std::move(o.value()));}
   const auto& object=objects.at(name);auto pc=row.at("offset").get<size_t>();auto decoded=decode_instruction(object.code,pc,dos_opcode_map(),name);
   if(!decoded.has_value())return decoded.error();
   if(decoded.value().opcode!=row.at("opcode").get<unsigned>())j["mismatches"].push_back({{"sample",i},{"reason","recorded opcode differs from source byte"}});
   j["decoded_samples"]=j["decoded_samples"].get<size_t>()+1;
   unsigned op=decoded.value().opcode;
   bool core=op==0||op==3||op==4||op==5||op==6||op==8||(op>=9&&op<=13)||(op>=17&&op<=37)||op==55||op==56||op==57||op==59||op==122;
   bool pair=i+1<rows.size()&&rows[i+1].at("object")==name&&rows[i+1].at("process")==row.at("process");
   if(!core||!pair){j["uncompared"].push_back({{"sample",i},{"opcode",op},{"reason",!core?"service/global/termination semantics outside this core slice":"next dispatch belongs to another process/object or is unavailable"}});continue;}
   Process p(object);p.pc=pc;p.stack=row.at("stack").get<std::vector<int32_t>>();
   if(p.stack.size()!=row.at("sp").get<size_t>())return DataError{"",name,pc,"reference stack depth mismatch"};
   Interpreter vm;RejectServices services;auto result=vm.step(p,services);
   if(!result.has_value())return result.error();
   const auto& next=rows[i+1];
   if(p.pc!=next.at("offset").get<size_t>()||p.stack!=next.at("stack").get<std::vector<int32_t>>())j["mismatches"].push_back({{"sample",i},{"object",name},{"opcode",op},{"offset",pc},{"native_next_pc",p.pc},{"reference_next_pc",next.at("offset")},{"native_stack",p.stack},{"reference_stack",next.at("stack")}});
   j["compared_core_transitions"]=j["compared_core_transitions"].get<size_t>()+1;
  }
 }catch(const nlohmann::json::exception&){return DataError{"","dispatch trace",0,"reference field missing or invalid"};}
 return j;
}
}
int replay_trace_command(int argc,char** argv) {
 if(argc!=5||std::string_view(argv[1])!="--reference"||std::string_view(argv[3])!="--report")return 2;
 auto r=hl::compare_dispatch_trace(argv[2],"work/import");
 if(!r.has_value()){auto& e=r.error();std::cerr<<e.resource<<":"<<e.byte_offset<<": "<<e.reason<<'\n';return 1;}
 std::ofstream out(argv[4]);out<<r.value().dump(2)<<'\n';return out&&r.value()["mismatches"].empty()?0:1;
}
