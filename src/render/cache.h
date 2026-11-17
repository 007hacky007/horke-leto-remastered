#pragma once
#include "data/result.h"
#include <list>
#include <memory>
#include <string>
namespace hl {
// External shared owners pin an entry until submitted draws have been flushed.
template<class T> class TextureCache {
 struct Entry {std::string key;size_t bytes;std::shared_ptr<T> value;};
 size_t budget_,bytes_{},insertions_{},evictions_{};std::list<Entry> entries_;
public:
 explicit TextureCache(size_t budget):budget_(budget){}
 size_t insertions()const{return insertions_;}size_t evictions()const{return evictions_;}
 size_t bytes()const{return bytes_;}size_t budget()const{return budget_;}
 std::shared_ptr<T> find(const std::string& key){for(auto i=entries_.begin();i!=entries_.end();++i)if(i->key==key){entries_.splice(entries_.begin(),entries_,i);return entries_.front().value;}return {};}
 Result<std::shared_ptr<T>> insert(std::string key,size_t bytes,std::shared_ptr<T> value){
  if(auto hit=find(key))return hit;
  if(!value||bytes>budget_)return DataError{"","cache",bytes,"texture exceeds cache budget"};
  // Check before evicting so a failed insertion leaves the cache usable.
  size_t free=budget_-bytes_;for(const auto& e:entries_)if(e.value.use_count()==1)free+=e.bytes;
  if(free<bytes)return DataError{"","cache",bytes,"cache budget pinned by in-flight textures"};
  for(auto i=entries_.end();bytes_+bytes>budget_;){--i;if(i->value.use_count()==1){bytes_-=i->bytes;i=entries_.erase(i);++evictions_;}}
  entries_.push_front({std::move(key),bytes,std::move(value)});bytes_+=bytes;++insertions_;return entries_.front().value;
 }
};
}
