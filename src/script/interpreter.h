#pragma once
#include "script/object.h"
#include <map>
#include <functional>
namespace hl {
Result<int32_t> dos_random(uint32_t& state,int32_t modulus);
enum class ProcessState {ready, waiting, terminated};
enum class StepOutcome {running, yielded, terminated};
struct AnimationSettings {
 int32_t z{},cycles{1},random{},periodic{},volume{100},pan{},x{},y{},phase_variable{-1},speed{100},sample_priority{100},delay{-1};
 bool loop{},phase_controlled{};
};
struct Process {
 AnimationSettings animation;
 const ScriptObject* object;size_t pc{};std::vector<int32_t> stack;
 ProcessState state{ProcessState::ready};int32_t wait_updates{};
 std::map<uint8_t,ScriptHandler> registered_handlers;
 explicit Process(const ScriptObject& source):object(&source){}
 Process(ScriptObject&&)=delete;
};
class EngineServices {
public:
 virtual ~EngineServices()=default;
 virtual Result<StepOutcome> invoke(Process& process,const Instruction& instruction)=0;
};
class Interpreter {
 std::map<std::string,int32_t> globals_;
 std::function<void(const Process&,const Instruction&)> trace_;
public:
 void reset_globals(){globals_.clear();}
 void set_trace(std::function<void(const Process&,const Instruction&)> trace){trace_=std::move(trace);}
 Result<void> event_global(std::string_view name,int32_t value,bool create=false);
 const auto& globals()const{return globals_;}
 Result<StepOutcome> step(Process& process,EngineServices& services);
 Result<StepOutcome> run(Process& process,EngineServices& services,size_t instruction_budget);
};
}
