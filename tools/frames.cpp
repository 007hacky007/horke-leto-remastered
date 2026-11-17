#include "assets.h"
#include "image_io.h"
#include "data/hash.h"
#include "data/grp.h"
#include <algorithm>
#include <fstream>
namespace hl {
Result<std::vector<uint8_t>> restore_frame(const CroppedFrame& f,uint32_t w,uint32_t h,uint8_t fill){
 if(!w||!h||uint64_t(w)*h>16777216||f.x>w||f.y>h||f.width>w-f.x||f.height>h-f.y||uint64_t(f.width)*f.height!=f.indices.size()||f.mask.size()!=f.indices.size()||(f.empty&&(!f.indices.empty()||f.width||f.height)))return DataError{"","frame",0,"invalid crop/canvas range"};
 std::vector<uint8_t> out(size_t(w)*h,fill);for(uint32_t y=0;y<f.height;++y)std::copy_n(f.indices.begin()+size_t(y)*f.width,f.width,out.begin()+size_t(y+f.y)*w+f.x);return out;
}
nlohmann::json frame_metadata(const IndexedFrame& f,const TransparencyRule& rule){return {{"index",f.frame_index},{"logical_width",f.logical_width},{"logical_height",f.logical_height},{"palette",f.palette},{"declared_delay",{{"numerator",f.declared_delay.numerator},{"denominator",f.declared_delay.denominator},{"unit","seconds"}}},{"effective_delay",{{"numerator",f.effective_delay.numerator},{"denominator",f.effective_delay.denominator},{"unit","engine_updates"}}},{"transparency_provenance",rule.provenance}};}
Result<nlohmann::json> export_flc(FlcDecoder& d,const TransparencyRule& rule,const std::filesystem::path& output){
 std::filesystem::create_directories(output);nlohmann::json manifest{{"schema_version",1},{"resource",d.resource()},{"source_sha256",sha256(d.source_bytes())},{"transparent_index",rule.index?nlohmann::json(*rule.index):nlohmann::json(nullptr)},{"provenance",rule.provenance},{"usage_rules",{{"flc_type_0_overlay","index 0 transparent"},{"first_full_screen_base","opaque copy of complete restored indices; palette index 0 remains opaque"}}},{"frames",nlohmann::json::array()}};d.rewind();
 for(;;){auto r=d.next();if(!r.has_value())return r.error();if(!r.value())break;auto& f=*r.value();auto crop=crop_frame(f,rule);if(!crop.has_value())return crop.error();auto& c=crop.value();auto j=frame_metadata(f,rule);j["crop"]={{"x",c.x},{"y",c.y},{"width",c.width},{"height",c.height},{"empty",c.empty}};j["offset"]={c.x,c.y};j["indices_sha256"]=sha256(std::as_bytes(std::span(f.indices)));std::string prefix="frame-"+std::to_string(f.frame_index);
  if(!c.empty){
   std::vector<uint8_t> rgba;for(size_t i=0;i<c.indices.size();++i){for(auto v:f.palette[c.indices[i]])rgba.push_back(v);rgba.push_back(c.mask[i]);}
   for(auto [suffix,channels,data]:{std::tuple{".png",4,std::span<const uint8_t>(rgba)},std::tuple{".indices.png",1,std::span<const uint8_t>(c.indices)},std::tuple{".mask.png",1,std::span<const uint8_t>(c.mask)}}){auto written=write_png(output/(prefix+suffix),c.width,c.height,channels,data);if(!written.has_value())return written.error();auto loaded=read_png(output/(prefix+suffix),channels);if(!loaded.has_value())return loaded.error();if(!std::equal(data.begin(),data.end(),loaded.value().pixels.begin(),loaded.value().pixels.end()))return DataError{"",d.resource(),f.frame_index,"PNG round-trip mismatch"};j[std::string(suffix)]={{"path",prefix+suffix},{"sha256",sha256_file(output/(prefix+suffix)).value()}};if(channels==1&&std::string_view(suffix)==".indices.png")c.indices=std::move(loaded.value().pixels);}
  }
  auto canvas=restore_frame(c,f.logical_width,f.logical_height,rule.index.value_or(0));if(!canvas.has_value())return canvas.error();if(canvas.value()!=f.indices)return DataError{"",d.resource(),f.frame_index,"crop reconstruction differs"};j["roundtrip_equal"]=true;manifest["frames"].push_back(j);
 }
 std::ofstream out(output.parent_path()/(d.resource()+".json"));out<<manifest.dump(2)<<'\n';if(!out)return DataError{"",d.resource(),0,"frame manifest write failed"};return manifest;
}
}

namespace hl {
Result<nlohmann::json> export_all_flc(const std::filesystem::path& data,const std::filesystem::path& output){
 auto a=GrpArchive::open(data/"GFX.GRP");if(!a.has_value())return a.error();
 nlohmann::json j{{"schema_version",1},{"files",0},{"frames",0},{"roundtrip_errors",0},{"resources",nlohmann::json::array()}};
 for(auto& e:a.value().entries())if(ascii_key(e.name).ends_with(".flc")){
  auto b=a.value().read(e.name);if(!b.has_value())return b.error();auto d=FlcDecoder::open(b.value(),e.name);if(!d.has_value())return d.error();
  auto r=export_flc(d.value(),{0,"analysis/evidence/transparency.json: FLC type 0 overlay; first full-screen layer may be copied opaque"},output/e.name);if(!r.has_value())return r.error();
  j["files"]=j["files"].get<unsigned>()+1;j["frames"]=j["frames"].get<unsigned>()+r.value()["frames"].size();j["resources"].push_back({{"name",e.name},{"frames",r.value()["frames"].size()},{"source_sha256",r.value()["source_sha256"]},{"manifest",e.name+".json"}});
 }std::ofstream out(output/"index.json");out<<j.dump(2)<<'\n';if(!out)return DataError{"","frames",0,"cannot write export index"};return j;
}
}
