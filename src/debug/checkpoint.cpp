#include "debug/checkpoint.h"
#include <algorithm>
namespace hl {
Result<nlohmann::json> compare_checkpoint(const nlohmann::json& reference,const nlohmann::json& native) {
 auto fail=[](std::string why)->Result<nlohmann::json>{return DataError{"","checkpoint",0,std::move(why)};};
 if(!reference.is_object()||!native.is_object()||reference.value("schema_version",0)!=1||native.value("schema_version",0)!=1)return fail("unsupported checkpoint schema");
 if(!reference.contains("scene")||!reference["scene"].is_string())return fail("reference scene observable missing");
 nlohmann::json differences=nlohmann::json::array(),excluded=nlohmann::json::array(),compared=nlohmann::json::array();
 for(const auto* key:{"scene","variables","inventory","active_dialogue","animation_positions","audio_positions"}) {
  if(!reference.contains(key)){excluded.push_back(key);continue;}
  if(reference[key].is_null()){excluded.push_back(key);continue;}
  if(!native.contains(key)||native[key].is_null())return fail(std::string("native observable missing: ")+key);
  auto a=reference[key],b=native[key];
  if(std::string_view(key)=="inventory") {
   if(!a.is_array()||!b.is_array())return fail("inventory must be an array");
   std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
  }
  compared.push_back(key);if(a!=b)differences.push_back({{"field",key},{"reference",a},{"native",b}});
 }
 return nlohmann::json{{"schema_version",1},{"match",differences.empty()},{"compared",compared},{"excluded",excluded},{"differences",differences}};
}
std::string canonical_checkpoint(const Checkpoint& c) {
 return nlohmann::json{{"schema_version",1},{"tick",c.tick},{"scene",c.scene},{"variables",c.variables},{"inventory",c.inventory},{"active_dialogue",c.active_dialogue},{"animation_positions",c.animation_positions},{"audio_positions",c.audio_positions}}.dump();
}
}
