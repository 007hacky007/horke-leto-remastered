#include "script/interpreter.h"
#include "data/grp.h"
#include <algorithm>
#include <bit>
#include <limits>
namespace hl {
namespace {
int32_t bits(uint32_t v){return std::bit_cast<int32_t>(v);}
}
Result<int32_t> dos_random(uint32_t& state,int32_t modulus) {
 if(!modulus)return DataError{"","random",0,"division by zero"};state=state*UINT32_C(0x41c64e6d)+UINT32_C(0x3039);return int32_t((state>>16)&0x7fff)%modulus;
}
Result<StepOutcome> Interpreter::step(Process& p,EngineServices& services) {
 auto fail=[&](std::string why)->Result<StepOutcome>{return DataError{"",p.object->name,p.pc,std::move(why),true};};
 if(p.state==ProcessState::terminated)return fail("process already terminated");
 if(p.state==ProcessState::waiting)return fail("process awaits scheduler/event");
 auto it=std::lower_bound(p.object->instructions.begin(),p.object->instructions.end(),p.pc,[](const auto& a,size_t b){return a.offset<b;});
 if(it==p.object->instructions.end()||it->offset!=p.pc)return fail("instruction pointer targets non-code");
 const auto ins=*it;unsigned op=ins.opcode;
 if(trace_)trace_(p,ins);
 if(!dos_opcode_map()[op].handler_address)return fail("unknown opcode "+std::to_string(op));
 // Stack effects come from DOS 0x27f0f-0x286e3, not later AGDS stubs.
 unsigned pops=0,pushes=0;
 if(op==3||op==5)pops=1;
 if(op==6){pops=1;pushes=2;}
 if(op>=9&&op<=13)pushes=1;
 if(op==14){pops=1;pushes=1;}
 if(op==15||op==16)pushes=1;
 if(op>=17&&op<=35){pops=2;pushes=1;}
 if(op==31||op==36||op==37){pops=1;pushes=1;}
 if(op>=38&&op<=41){pops=1;pushes=1;}
 if(op==42){pops=1;}if(op==43){pops=2;}
 if(op>=44&&op<=53)pops=1;
 if(op==122||op==124||op==125||op==126||op==127||op==128||op==130||op==132||op==165||op==167)pops=1;
 if(op==129)pops=2;
 if(p.stack.size()<pops)return fail("stack underflow");
 if(p.stack.size()-pops+pushes>100)return fail("stack overflow");
 auto pop=[&](){auto v=p.stack.back();p.stack.pop_back();return v;};
 auto resource=[&](int32_t index)->Result<std::string>{
  if(index<0||size_t(index)>=p.object->layout.resources.size())return DataError{"",p.object->name,p.pc,"global resource index outside table"};
  return p.object->layout.resources[size_t(index)].name;
 };
 if(op==0) {p.pc=ins.next_offset;return StepOutcome::running;}
 if(op==3){bool jump=pop()==0;p.pc=jump?*ins.branch_target:ins.next_offset;return StepOutcome::running;}
 if(op==4){p.pc=*ins.branch_target;return StepOutcome::running;}
 if(op==5)pop();
 else if(op==6)p.stack.push_back(p.stack.back());
 else if(op==7){p.pc=ins.next_offset;p.state=ProcessState::terminated;return StepOutcome::terminated;}
 else if(op==8){p.pc=ins.next_offset;return StepOutcome::yielded;}
 else if(op>=9&&op<=13)p.stack.push_back(int32_t(ins.operand));
 else if(op>=14&&op<=16) {
  int32_t index=op==14?p.stack.back():int32_t(ins.operand);auto key=resource(index);if(!key.has_value())return key.error();
  auto g=globals_.find(key.value());if(g==globals_.end())return fail("global used before definition: "+key.value());
  if(op==14)pop();p.stack.push_back(g->second);
 }
 else if(op>=17&&op<=37) {
  int32_t a=p.stack.back(),b=pops==2?p.stack[p.stack.size()-2]:0,result=0;
  if((op==26||op==27)&&(a==0||(b==INT32_MIN&&a==-1)))return fail(a==0?"division by zero":"signed division overflow");
  switch(op) {
   case 17:result=b==a;break;case 18:result=b!=a;break;
   case 19:result=b>a;break;case 20:result=b<a;break;case 21:result=b>=a;break;case 22:result=b<=a;break;
   case 23:result=bits(uint32_t(b)+uint32_t(a));break;case 24:result=bits(uint32_t(b)-uint32_t(a));break;case 25:result=bits(uint32_t(b)*uint32_t(a));break;
   case 26:result=b/a;break;case 27:result=b%a;break;
   case 28:result=b&a;break;case 29:result=b|a;break;case 30:result=b^a;break;case 31:result=~a;break;
   case 32:result=bits(uint32_t(b)<<(uint32_t(a)&31));break;case 33:result=b>>(uint32_t(a)&31);break;
   case 34:result=bool(b)&&bool(a);break;case 35:result=bool(b)||bool(a);break;case 36:result=!a;break;case 37:result=bits(0-uint32_t(a));break;
  }
  p.stack.resize(p.stack.size()-pops);p.stack.push_back(result);
 }
 else if(op>=38&&op<=53) {
  auto index=p.stack.back();auto key=resource(index);if(!key.has_value())return key.error();
  auto g=globals_.find(key.value());if(op!=42&&op!=43&&g==globals_.end())return fail("global used before definition: "+key.value());
  if((op==42||op==43)&&g==globals_.end()&&globals_.size()>=1000)return fail("too many global variables (1000)");
  if(op==43){int32_t value=p.stack[p.stack.size()-2];globals_[key.value()]=value;pop();pop();}
  else if(op==42) {if(p.stack.size()<2)return fail("stack underflow");int32_t value=p.stack[p.stack.size()-2];globals_[key.value()]=value;pop();}
  else if(op<=41) {
   int32_t before=g->second;g->second=bits(uint32_t(before)+(op==38||op==40?1u:UINT32_MAX));pop();p.stack.push_back(op<=39?g->second:before);
  } else {
   if(p.stack.size()<2)return fail("stack underflow");int32_t a=p.stack[p.stack.size()-2],b=g->second,result=0;
   if((op==47||op==48)&&(a==0||(b==INT32_MIN&&a==-1)))return fail(a==0?"division by zero":"signed division overflow");
   switch(op) {
    case 44:result=bits(uint32_t(b)+uint32_t(a));break;case 45:result=bits(uint32_t(b)-uint32_t(a));break;case 46:result=bits(uint32_t(b)*uint32_t(a));break;
    case 47:result=b/a;break;case 48:result=b%a;break;case 49:result=bits(uint32_t(b)<<(uint32_t(a)&31));break;case 50:result=b>>(uint32_t(a)&31);break;
    case 51:result=b&a;break;case 52:result=b|a;break;case 53:result=b^a;break;
   }
   g->second=result;pop();
  }
 }
 else if(op==55||op==56||op==57||op==59) {
  p.registered_handlers[uint8_t(op)]={uint8_t(op),ins.next_offset,*ins.branch_target};p.pc=*ins.branch_target;return StepOutcome::running;
 }
 else if(op==123)p.animation=AnimationSettings{};
 else if(op==114)p.animation.phase_controlled=true;
 else if(op==131){p.animation.loop=true;p.animation.cycles=1000000000;}
 else if(op==124)p.animation.z=pop();
 else if(op==125)p.animation.cycles=pop();
 else if(op==126)p.animation.random=pop();
 else if(op==127)p.animation.periodic=pop();
 else if(op==128)p.animation.volume=pop();
 else if(op==129){p.animation.y=pop();p.animation.x=pop();}
 else if(op==132)p.animation.speed=pop();
 else if(op==165)p.animation.delay=pop();
 else if(op==167)p.animation.sample_priority=pop();
 else if(op==122) {p.wait_updates=pop();p.pc=ins.next_offset;return p.wait_updates?StepOutcome::yielded:StepOutcome::running;}
 else {
  auto result=services.invoke(p,ins);
  if(!result.has_value()){auto e=result.error();if(!e.script_provenance){if(e.file.empty())e.file=e.resource;e.resource=p.object->name;e.byte_offset=ins.offset;e.script_provenance=true;}return e;}
  p.pc=ins.branch_target&&ins.flow==InstructionFlow::declaration?*ins.branch_target:ins.next_offset;return result.value();
 }
 p.pc=ins.next_offset;return StepOutcome::running;
}
Result<void> Interpreter::event_global(std::string_view name,int32_t value,bool create) {
 auto i=globals_.find(std::string(name));if(i==globals_.end()){if(!create)return DataError{"",std::string(name),0,"event global is undefined"};if(globals_.size()>=1000)return DataError{"",std::string(name),0,"too many global variables (1000)"};globals_[std::string(name)]=value;}else i->second=value;return {};
}
Result<StepOutcome> Interpreter::run(Process& p,EngineServices& services,size_t budget) {
 for(size_t n=0;n<budget;++n){auto r=step(p,services);if(!r.has_value()||r.value()!=StepOutcome::running)return r;}
 return DataError{"",p.object->name,p.pc,"instruction budget exhausted"};
}
}
