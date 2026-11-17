#include "le/le_fixups.h"
#include <map>
#include <stdexcept>
namespace hl {
Result<RelocatedImage> apply_fixups(const LeImage& image) {
 try {
    RelocatedImage out{image.objects,image.entry,{}};
    std::map<std::pair<unsigned,unsigned>,std::byte> writes;
    for(unsigned page=0;page<image.page_count;++page) {
        std::size_t p=image.fixup_pages.at(page),end=image.fixup_pages.at(page+1);
        auto u=[&](unsigned n) {
            if(p>end || n>end-p || end>image.fixup_records.size())throw std::runtime_error("truncated fixup record");
            unsigned v=0;for(unsigned j=0;j<n;++j)v|=std::to_integer<unsigned>(image.fixup_records[p++])<<(8*j);return v;
        };
        while(p<end) {
            const auto type=u(1),flags=u(1);
            // Only the two offset32 internal forms observed in this executable are admitted.
            if(type!=7 || (flags!=0 && flags!=0x10))throw std::runtime_error("unsupported LE fixup form");
            const auto source=static_cast<std::int16_t>(u(2));
            const auto target_object=u(1);const auto target_offset=u(flags&0x10?4:2);
            if(!target_object || target_object>out.objects.size())throw std::runtime_error("invalid fixup target object");
            const auto& target=out.objects[target_object-1];
            if(target_offset>=target.bytes.size() || std::uint64_t(target.base)+target_offset>0xffffffffull)throw std::runtime_error("fixup target overflow");
            std::size_t object=0;
            while(object<out.objects.size()) {
                const auto& o=out.objects[object];
                if(o.page_count && page+1>=o.first_page && page+1<o.first_page+o.page_count)break;
                ++object;
            }
            if(object==out.objects.size())throw std::runtime_error("fixup page has no object");
            auto& o=out.objects[object];
            const std::int64_t offset=std::int64_t(page+1-o.first_page)*image.page_size+source;
            if(offset<0 || std::uint64_t(offset)+4>o.bytes.size())throw std::runtime_error("fixup source outside object");
            const auto value=target.base+target_offset;
            for(unsigned j=0;j<4;++j) {
                auto byte=std::byte((value>>(8*j))&255);auto key=std::pair{unsigned(object),unsigned(offset)+j};
                auto [it,inserted]=writes.emplace(key,byte);
                if(!inserted && it->second!=byte)throw std::runtime_error("conflicting fixup writes");
                o.bytes[offset+j]=byte;
            }
            out.fixups.push_back({page,unsigned(object+1),unsigned(offset),target_object,target_offset,value,flags});
        }
    }
    return out;
 }catch(const std::exception& e){return DataError{"LE","fixups",0,e.what()};}
}
}
