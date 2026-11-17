#pragma once
#include "script/interpreter.h"
namespace hl {
struct SampleSchedule {
 int32_t cycles{},periodic{},random{},countdown{};bool start_now{},done{};
 static Result<SampleSchedule> begin(int32_t cycles,int32_t delay,int32_t periodic,int32_t random,uint32_t& rng){
  if(cycles<=0||cycles==INT32_MAX||delay< -1||periodic<0||random<0||(random&&random<=periodic))return DataError{"","sample",0,"invalid DOS sample schedule"};
  SampleSchedule s{cycles,periodic,random,0,delay==0||(delay<0&&periodic==0),false};if(delay>0){++s.cycles;s.countdown=delay;}else s.gap(rng);return s;
 }
 void gap(uint32_t& rng){countdown=periodic;if(periodic&&random)countdown+=dos_random(rng,random-periodic).value();}
 bool advance(bool playing,uint32_t& rng){if(done||playing)return false;if(countdown>0){--countdown;return false;}if(--cycles<=0){done=true;return false;}gap(rng);return true;}
};
}
