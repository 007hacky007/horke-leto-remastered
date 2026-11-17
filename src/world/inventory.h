#pragma once
#include "data/result.h"
#include <array>
#include <algorithm>
#include <vector>
#include <optional>
namespace hl {
struct InventorySlot {std::string item;uint32_t count{};};
class Inventory {
 std::array<InventorySlot,35> slots_{};
public:
 Result<void> add(std::string item,std::optional<unsigned> preferred={}){for(auto& s:slots_)if(s.item==item){if(s.count==UINT32_MAX)return DataError{"","inventory",0,"item count overflow"};++s.count;return {};}
  if(std::all_of(slots_.begin(),slots_.end(),[](const auto& s){return !s.count;})){slots_[17]={std::move(item),1};return {};}
  if(preferred&&*preferred<35&&*preferred!=17&&!slots_[*preferred].count){slots_[*preferred]={std::move(item),1};return {};}
  for(int i=34;i>=0;--i)if(i!=17&&slots_[i].count==0){slots_[i]={std::move(item),1};return {};}
  return DataError{"","inventory",0,"inventory capacity exceeded (34 plus eye)"};}
 bool remove(std::string_view id){for(auto& s:slots_)if(s.item==id&&s.count){if(!--s.count)s.item.clear();return true;}return false;}
 void clear(){auto eye=slots_[17];slots_={};slots_[17]=std::move(eye);}
 const auto& slots()const{return slots_;}
 std::vector<std::string> items()const{std::vector<std::string> v;for(const auto& s:slots_)for(uint32_t i=0;i<s.count;++i)v.push_back(s.item);return v;}
 static std::optional<unsigned> hit(int x,int y){if(x<11||x>627||y<18||y>463)return {};unsigned slot=unsigned((x-11)*7/616+(y-18)*5/445*7);if(slot>=35)return {};return slot;}
};
}
