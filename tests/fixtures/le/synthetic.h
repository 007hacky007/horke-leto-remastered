#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
namespace fixture {
inline void put(std::vector<std::byte>& b, std::size_t p, std::uint32_t v, unsigned n=4) {
    for (unsigned i=0;i<n;++i) b.at(p+i)=std::byte((v>>(8*i))&255);
}
inline std::vector<std::byte> le() {
    std::vector<std::byte> b(0x440);
    put(b,0,0x5a4d,2); put(b,0x3c,0x80); put(b,0x80,0x454c,2);
    auto h=[&](unsigned p,unsigned v){put(b,0x80+p,v);};
    h(0x14,3);h(0x18,1);h(0x1c,7);h(0x28,16);h(0x2c,16);
    h(0x40,0xc4);h(0x44,2);h(0x48,0xf4);h(0x68,0x100);h(0x6c,0x120);h(0x80,0x400);
    for(auto [p,v]: {std::pair{0x144,40}, {0x148,0x10000}, {0x14c,0x2045}, {0x150,1}, {0x154,2},
                     {0x15c,16}, {0x160,0x20000}, {0x164,0x2043}, {0x168,3}, {0x16c,1}})put(b,p,v);
    put(b,0x174,0x00010000);put(b,0x178,0x00030000);put(b,0x17c,0x03000000);
    for(unsigned i=0;i<16;++i) {b[0x400+i]=std::byte(0xa0+i);b[0x420+i]=std::byte(0xc0+i);}
    return b;
}
inline std::vector<std::byte> fixup(bool wide=false, int source=4, unsigned object=2,unsigned target=8) {
    auto b=le(); auto length=wide?9:7;
    put(b,0x184,length);put(b,0x188,length);put(b,0x18c,length);
    put(b,0x1a0,7,1);put(b,0x1a1,wide?16:0,1);put(b,0x1a2,static_cast<unsigned>(source),2);
    put(b,0x1a4,object,1);put(b,0x1a5,target,wide?4:2);return b;
}
}
