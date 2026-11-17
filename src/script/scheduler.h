#pragma once
#include "script/interpreter.h"
#include <array>
#include <memory>
namespace hl {
using GameTick=uint64_t;
class Scheduler {
 Interpreter& interpreter_;EngineServices& services_;
 std::array<std::unique_ptr<Process>,100> processes_{};
 std::array<uint64_t,100> scenes_{};uint64_t scene_{};std::optional<GameTick> last_tick_;
public:
 Scheduler(Interpreter& interpreter,EngineServices& services):interpreter_(interpreter),services_(services){}
 Result<size_t> spawn(const ScriptObject& object,size_t entry,uint64_t scene);
 Result<void> advance(GameTick tick,size_t instruction_budget=100000);
 Result<void> wake(size_t slot);
 void set_scene(uint64_t scene){scene_=scene;}
 Process& process(size_t slot){return *processes_.at(slot);}
};
}
