#include "le/le_image.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace hl {
namespace {
struct Reader {
    ByteView b;
    ByteView take(std::uint64_t p, std::uint64_t n) const {
        if(p>b.size() || n>b.size()-p)throw std::out_of_range("truncated LE range at "+std::to_string(p));
        return b.subspan(static_cast<std::size_t>(p),static_cast<std::size_t>(n));
    }
    std::uint32_t u(std::uint64_t p,unsigned n=4)const {
        auto s=take(p,n);std::uint32_t v=0;
        for(unsigned i=0;i<n;++i)v|=std::to_integer<std::uint32_t>(s[i])<<(8*i);return v;
    }
};
}
Result<LeImage> parse_le(ByteView input) {
 try {
    Reader r{input}; if(r.u(0,2)!=0x5a4d)throw std::runtime_error("not MZ");
    const std::uint64_t h=r.u(0x3c);
    r.take(h,0xc4);
    if(r.u(h,2)!=0x454c || r.u(h+2,2)!=0)throw std::runtime_error("unsupported LE signature/byte order");
    const auto size=r.u(h+0x28),pages=r.u(h+0x14),count=r.u(h+0x44);
    if(!size || size>65536 || !count || count>256 || pages>1000000)throw std::runtime_error("invalid LE dimensions");
    const std::uint64_t ot=h+r.u(h+0x40),pm=h+r.u(h+0x48),fp=h+r.u(h+0x68),fr=h+r.u(h+0x6c),dp=r.u(h+0x80);
    r.take(ot,24ull*count);r.take(pm,4ull*pages);r.take(fp,4ull*(pages+1));
    LeImage out;out.page_size=size;out.page_count=pages;
    for(std::uint32_t i=0;i<=pages;++i) {
        auto v=r.u(fp+4ull*i);
        if(i && v<out.fixup_pages.back())throw std::runtime_error("backward fixup offsets");
        out.fixup_pages.push_back(v);
    }
    if(out.fixup_pages.front()!=0)throw std::runtime_error("unaccounted fixup prefix");
    auto records=r.take(fr,out.fixup_pages.back());out.fixup_records.assign(records.begin(),records.end());
    std::uint64_t allocated=0;
    std::vector<bool> mapped(pages,false);
    for(std::uint32_t i=0;i<count;++i) {
        const auto p=ot+24ull*i;
        auto virtual_size=r.u(p),base=r.u(p+4),flags=r.u(p+8),first=r.u(p+12),n=r.u(p+16);
        allocated+=virtual_size;
        if(allocated>256ull*1024*1024 || std::uint64_t(base)+virtual_size>0x100000000ull)throw std::runtime_error("object size/address overflow");
        if(n && (!first || std::uint64_t(first)-1+n>pages || std::uint64_t(n-1)*size>=virtual_size))throw std::runtime_error("invalid object pages");
        for(auto& other:out.objects) if(std::uint64_t(base)<std::uint64_t(other.base)+other.bytes.size() && std::uint64_t(other.base)<std::uint64_t(base)+virtual_size)throw std::runtime_error("overlapping objects");
        LeObject obj{base,flags,first,n,std::vector<std::byte>(virtual_size)};
        for(std::uint32_t j=0;j<n;++j) {
            const auto logical=first-1+j;
            if(mapped[logical])throw std::runtime_error("duplicate logical page");mapped[logical]=true;
            auto record=r.take(pm+4ull*logical,4);
            const auto physical=(std::to_integer<unsigned>(record[0])<<16)|(std::to_integer<unsigned>(record[1])<<8)|std::to_integer<unsigned>(record[2]);
            const auto kind=std::to_integer<unsigned>(record[3]);
            if(kind==3)continue;
            if(kind!=0 || !physical || physical>pages)throw std::runtime_error("unsupported LE page encoding");
            auto bytes=size;
            if(physical==pages) {auto last=r.u(h+0x2c);if(last>size)throw std::runtime_error("invalid final page size");if(last)bytes=last;}
            auto source=r.take(dp+std::uint64_t(physical-1)*size,bytes);
            auto offset=std::uint64_t(j)*size;
            auto copy=std::min<std::uint64_t>(source.size(),obj.bytes.size()-offset);
            std::copy_n(source.begin(),copy,obj.bytes.begin()+offset);
        }
        out.objects.push_back(std::move(obj));
    }
    if(std::find(mapped.begin(),mapped.end(),false)!=mapped.end())throw std::runtime_error("unaccounted logical pages");
    auto entry_object=r.u(h+0x18),entry_offset=r.u(h+0x1c);
    if(!entry_object || entry_object>out.objects.size() || entry_offset>=out.objects[entry_object-1].bytes.size())throw std::runtime_error("invalid LE entry");
    out.entry=out.objects[entry_object-1].base+entry_offset;
    return out;
 }catch(const std::exception& e){return DataError{"LE","",0,e.what()};}
}
}
