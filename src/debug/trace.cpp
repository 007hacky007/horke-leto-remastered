#include "debug/trace.h"
#include <nlohmann/json.hpp>
#include <fstream>
namespace hl {
Result<std::vector<InputEvent>> read_trace(std::istream& input) {
 std::vector<InputEvent> events; std::string line; std::size_t number=0;
 while(std::getline(input,line)) {
  ++number;
  try {
   auto j=nlohmann::json::parse(line);
   if(j.at("schema_version")!=1 || !j.at("tick").is_number_unsigned()) throw std::runtime_error("invalid version/tick");
   InputEvent e; e.tick=j.at("tick").get<std::uint64_t>(); e.kind=j.at("kind").get<std::string>();
   if(!events.empty() && e.tick < events.back().tick) throw std::runtime_error("backward tick");
   if(e.kind=="key_down" || e.kind=="key_up") {
    e.key=j.at("key").get<std::string>(); if(e.key.empty()) throw std::runtime_error("empty key");
   } else if(e.kind=="text_input") {
    e.text=j.at("text").get<std::string>();if(e.text.empty()||e.text.size()>4096||e.text.find('\0')!=std::string::npos)throw std::runtime_error("invalid text input extent");
   } else if(e.kind=="mouse_down" || e.kind=="mouse_up" || e.kind=="mouse_move") {
    if(!j.at("logical_x").is_number_integer() || !j.at("logical_y").is_number_integer()) throw std::runtime_error("invalid coordinates");
    auto x=j.at("logical_x").get<std::int64_t>(),y=j.at("logical_y").get<std::int64_t>();
    if(x<0 || x>=640 || y<0 || y>=480) throw std::runtime_error("coordinates outside 640x480");
    e.logical_x=static_cast<int>(x);e.logical_y=static_cast<int>(y);
    if(e.kind!="mouse_move") { e.button=j.at("button").get<std::string>(); if(e.button!="left" && e.button!="right" && e.button!="middle") throw std::runtime_error("invalid button"); }
   } else throw std::runtime_error("invalid input kind");
   events.push_back(std::move(e));
  } catch(const std::exception& e) { return DataError{"trace","",number,e.what()}; }
 }
 if(input.bad()) return DataError{"trace","",number,"read failed"};
 return events;
}
Result<std::vector<InputEvent>> read_trace(const std::string& path) {
 std::ifstream input(path); if(!input) return DataError{path,"",0,"open failed"};
 return read_trace(input);
}
}
