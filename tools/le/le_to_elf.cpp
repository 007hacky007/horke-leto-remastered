#include "le/le_to_elf.h"
#include <fstream>
#include <limits>
namespace hl {
Result<std::vector<std::byte>> make_elf(const RelocatedImage& image) {
 if(image.objects.empty() || image.objects.size()>256)return DataError{"ELF","",0,"invalid object count"};
 const auto count=static_cast<unsigned>(image.objects.size());
 std::vector<std::byte> b(52+32*count);
 auto put=[&](std::size_t p,std::uint32_t v,unsigned n=4){for(unsigned j=0;j<n;++j)b.at(p+j)=std::byte((v>>(8*j))&255);};
 put(0,0x464c457f);put(4,1,1);put(5,1,1);put(6,1,1);put(16,2,2);put(18,3,2);put(20,1);put(24,image.entry);put(28,52);put(40,52,2);put(42,32,2);put(44,count,2);put(46,40,2);put(48,count+2,2);put(50,count+1,2);
 std::vector<unsigned> offsets;
 for(unsigned i=0;i<count;++i) {
    const auto& o=image.objects[i];while(b.size()%4096 != o.base%4096)b.push_back(std::byte{0});
    if(b.size()+o.bytes.size()>0xffffffffull)return DataError{"ELF","",0,"ELF offset overflow"};
    unsigned offset=static_cast<unsigned>(b.size());offsets.push_back(offset);b.insert(b.end(),o.bytes.begin(),o.bytes.end());
    auto p=52+32*i;put(p,1);put(p+4,offset);put(p+8,o.base);put(p+12,o.base);put(p+16,o.bytes.size());put(p+20,o.bytes.size());put(p+24,4|((o.flags&2)?2:0)|((o.flags&4)?1:0));put(p+28,4096);
 }
 const std::string names("\0.text\0.data\0.shstrtab\0",23);
 unsigned name_offset=b.size();for(unsigned char c:names)b.push_back(std::byte{c});
 while(b.size()%4)b.push_back(std::byte{0});unsigned sh=b.size();b.resize(b.size()+40*(count+2));put(32,sh);
 for(unsigned i=0;i<count;++i) {
    auto p=sh+40*(i+1);auto& o=image.objects[i];put(p,i==0?1:7);put(p+4,1);put(p+8,2|((o.flags&2)?1:0)|((o.flags&4)?4:0));put(p+12,o.base);put(p+16,offsets[i]);put(p+20,o.bytes.size());put(p+32,16);
 }
 auto p=sh+40*(count+1);put(p,13);put(p+4,3);put(p+16,name_offset);put(p+20,names.size());put(p+32,1);
 return b;
}
Result<void> write_elf(const RelocatedImage& image,const std::filesystem::path& path) {
 auto r=make_elf(image);if(!r.has_value())return r.error();std::ofstream f(path,std::ios::binary);
 f.write(reinterpret_cast<const char*>(r.value().data()),r.value().size());if(!f)return DataError{"ELF","",0,"output write failed"};return {};
}
}
