#pragma once
#include "archive.h"
#include <initializer_list>
namespace fixture {
using Bytes=std::vector<std::byte>;
inline Bytes raw(std::initializer_list<unsigned> values){Bytes b;for(auto v:values)b.push_back(std::byte(v));return b;}
inline Bytes chunk(unsigned type,Bytes payload){Bytes b(6+payload.size());u32(b,0,b.size());u16(b,4,type);std::copy(payload.begin(),payload.end(),b.begin()+6);return b;}
inline Bytes frame(std::vector<Bytes> chunks){Bytes b(16);u16(b,4,0xf1fa);u16(b,6,chunks.size());for(auto& c:chunks)b.insert(b.end(),c.begin(),c.end());u32(b,0,b.size());return b;}
inline Bytes flc(std::vector<Bytes> frames,unsigned declared=1,unsigned depth=8,unsigned delay=71){Bytes b(128);u16(b,4,0xaf12);u16(b,6,declared);u16(b,8,2);u16(b,10,2);u16(b,12,depth);u32(b,16,delay);for(auto& f:frames)b.insert(b.end(),f.begin(),f.end());u32(b,0,b.size());return b;}
}
