#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
namespace script_fixture {
inline void word(std::vector<std::byte>& b, size_t p, uint16_t v) {
 b[p]=std::byte(v&255); b[p+1]=std::byte(v>>8);
}
inline std::vector<std::byte> object(std::initializer_list<unsigned> body,const std::vector<std::string>& names={"x"}) {
 std::vector<std::byte> c(17);
 c[0]=std::byte{0}; word(c,1,0xdead); word(c,3,12);
 for(auto v:body)c.push_back(std::byte(v));
 size_t descriptor=c.size(), table=descriptor+5;
 c.resize(table+names.size()*4);
 c[descriptor]=std::byte{1}; word(c,descriptor+1,uint16_t(names.size()));
 word(c,11,uint16_t(table-22)); word(c,13,uint16_t(names.size()));
 for(size_t i=0;i<names.size();++i){word(c,table+i*4,uint16_t(c.size()-table));word(c,table+i*4+2,0x80);for(auto ch:names[i])c.push_back(std::byte(ch));c.push_back(std::byte{});}
 word(c,15,uint16_t(c.size()-3)); word(c,descriptor+3,uint16_t(c.size()-3));
 std::vector<std::byte> out(7);word(out,0,7);word(out,4,uint16_t(c.size()));out[6]=std::byte{1};
 out.insert(out.end(),c.begin(),c.end());return out;
}
}
