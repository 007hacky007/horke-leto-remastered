#include "script/scheduler.h"
#include <algorithm>
#include <bit>
namespace hl {
Result<size_t> Scheduler::spawn(const ScriptObject& object,size_t entry,uint64_t scene) {
 if(std::none_of(object.instructions.begin(),object.instructions.end(),[&](const auto& i){return i.offset==entry;}))return DataError{"",object.name,entry,"process entry is not an instruction"};
 for(size_t i=0;i<processes_.size();++i)if(!processes_[i]||processes_[i]->state==ProcessState::terminated) {
  processes_[i]=std::make_unique<Process>(object);processes_[i]->pc=entry;scenes_[i]=scene;return i;
 }
 return DataError{"",object.name,entry,"process table full (100 slots)"};
}
Result<void> Scheduler::advance(GameTick tick,size_t budget) {
 if(last_tick_&&tick<=*last_tick_)return DataError{"","scheduler",0,"updates must be strictly ordered"};
 last_tick_=tick;
 // DOS 0x2a613 visits ascending slots belonging to the current scene.
 for(size_t i=0;i<processes_.size();++i) {
  if(!processes_[i]||scenes_[i]!=scene_)continue;
  auto& p=*processes_[i];if(p.state!=ProcessState::ready)continue;
  // DOS 0x27b96 decrements once and returns even when this reaches zero.
  if(p.wait_updates!=0){p.wait_updates=std::bit_cast<int32_t>(uint32_t(p.wait_updates)-1);continue;}
  auto r=interpreter_.run(p,services_,budget);if(!r.has_value())return r.error();
 }
 return {};
}
Result<void> Scheduler::wake(size_t slot) {
 if(slot>=processes_.size()||!processes_[slot]||processes_[slot]->state!=ProcessState::waiting)return DataError{"","scheduler",slot,"event does not identify a waiting process"};
 processes_[slot]->state=ProcessState::ready;return {};
}
}
