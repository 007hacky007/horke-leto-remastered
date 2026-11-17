#include "script/object.h"
#include <algorithm>
#include <set>
namespace hl {
Result<ScriptObject> load_object(ByteView bytes,std::string_view name) {
 auto fail=[&](size_t p,std::string why)->Result<ScriptObject>{return DataError{"",std::string(name),p,std::move(why)};};
 auto layout=inspect_object_header(bytes);
 if(!layout.has_value()){auto e=layout.error();e.resource=name;return e;}
 ScriptObject out;out.name=name;out.layout=std::move(layout.value());
 auto code=bytes.subspan(7);out.code.assign(code.begin(),code.end());
 if(out.layout.table_offset<22)return fail(11,"resource table overlaps Enter metadata");
 // The five-byte resource descriptor is separate from the four-byte name records.
 out.executable_end=out.layout.table_offset-5;
 auto word=[&](size_t p){return uint16_t(std::to_integer<uint8_t>(code[p]))|(uint16_t(std::to_integer<uint8_t>(code[p+1]))<<8);};
 size_t descriptor=out.executable_end;
 if(code[descriptor]!=std::byte{1}||word(descriptor+1)!=out.layout.resources.size()||word(descriptor+3)!=word(15))
  return fail(descriptor,"resource descriptor does not match object header/table");
 auto map=dos_opcode_map();std::set<size_t> starts;
 for(size_t p=0;p<out.executable_end;) {
  auto r=decode_instruction(code.first(out.executable_end),p,map,name);
  if(!r.has_value())return r.error();
  auto ins=r.value();starts.insert(p);out.instructions.push_back(ins);
  if(ins.flow==InstructionFlow::enter&&p!=0)return fail(p,"unexpected nested Enter metadata");
  if(ins.flow==InstructionFlow::declaration) {
   if(!ins.branch_target||*ins.branch_target<=ins.next_offset||*ins.branch_target>out.executable_end)return fail(p,"handler byte length outside code");
   out.handlers.push_back({ins.opcode,ins.next_offset,*ins.branch_target});
  }
  p=ins.next_offset;
 }
 for(const auto& ins:out.instructions) {
  if(ins.branch_target) {
   bool terminal_handler=ins.flow==InstructionFlow::declaration&&*ins.branch_target==out.executable_end;
   if(!terminal_handler&&(!starts.contains(*ins.branch_target)||*ins.branch_target==0))return fail(ins.offset,"branch targets non-code or an operand");
  }
 }
 // Build a control-flow walk from initialization and every separately declared handler.
 std::set<size_t> visited;std::vector<size_t> pending{0};
 for(const auto& h:out.handlers)pending.push_back(h.entry);
 while(!pending.empty()) {
  size_t p=pending.back();pending.pop_back();if(!visited.insert(p).second)continue;
  auto it=std::lower_bound(out.instructions.begin(),out.instructions.end(),p,[](const auto& a,size_t b){return a.offset<b;});
  if(it==out.instructions.end()||it->offset!=p)return fail(p,"control flow leaves executable code");
  const auto& ins=*it;
  if(ins.flow==InstructionFlow::terminate)continue;
  if(ins.flow==InstructionFlow::jump||ins.flow==InstructionFlow::declaration)pending.push_back(*ins.branch_target);
  else {
   pending.push_back(ins.next_offset);
   if(ins.flow==InstructionFlow::conditional)pending.push_back(*ins.branch_target);
  }
 }
 return out;
}
}
