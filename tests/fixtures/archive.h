#pragma once
#include <vector>
#include <cstddef>
#include <string_view>
namespace fixture {
inline void u16(std::vector<std::byte>& b,size_t p,unsigned v){b.at(p)=std::byte(v&255);b.at(p+1)=std::byte(v>>8);}
inline void u32(std::vector<std::byte>& b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b.at(p+i)=std::byte((v>>(i*8))&255);}
inline void text(std::vector<std::byte>& b,size_t p,std::string_view s){for(size_t i=0;i<s.size();++i)b.at(p+i)=std::byte(s[i]);}
inline std::vector<std::byte> grp(){std::vector<std::byte>b(107);text(b,0,std::string_view("AGDS group file\x1a",16));u32(b,16,44);u32(b,20,0x1a03c9e6);u32(b,24,1);u32(b,28,1);u32(b,32,2);text(b,44,"MiXeD.FLC");u32(b,58,104);u32(b,62,3);text(b,104,"abc");return b;}
}
