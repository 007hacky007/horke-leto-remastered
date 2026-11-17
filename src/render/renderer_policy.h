#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
namespace hl {
inline bool valid_renderer_preference(std::string_view value){return value=="auto"||value=="gpu"||value=="software";}
inline bool software_d3d12_adapter(std::string_view driver,std::string name){
 if(driver!="direct3d12")return false;
 std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
 return name=="microsoft basic render driver"||name=="microsoft warp"||name=="warp";
}
class StartupRenderBudget {
 unsigned frames_{},overruns_{};
public:
 bool observe(double work_ms){
  ++frames_;if(frames_<=30||frames_>90)return false;
  if(work_ms>1000.0/35)++overruns_;
  return frames_==90&&overruns_>=30;
 }
};
}
