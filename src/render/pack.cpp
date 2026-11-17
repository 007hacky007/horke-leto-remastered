#include "render/pack.h"
#include "data/hash.h"
#include "data/grp.h"
#include "data/text.h"
#include "data/font.h"
#include <fstream>
#include <set>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"
namespace hl {
Result<SourceCatalog> source_catalog(const std::filesystem::path& data){auto archive=GrpArchive::open(data/"GFX.GRP");if(!archive.has_value())return archive.error();SourceCatalog out;for(const auto& e:archive.value().entries())if(ascii_key(e.name).ends_with(".flc")){auto bytes=archive.value().read(e.name);if(!bytes.has_value())return bytes.error();auto d=FlcDecoder::open(bytes.value(),e.name);if(!d.has_value())return d.error();out[e.name]={sha256(bytes.value()),d.value().declared_frames()};}return out;}
std::string resource_key(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c-=32;return s;}
Result<std::filesystem::path> safe_pack_path(const std::filesystem::path& root,const std::string& relative){
 auto bad=[&]()->Result<std::filesystem::path>{return DataError{"",relative,0,"unsafe pack path"};};
 if(relative.empty()||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos||relative.find('\0')!=std::string::npos)return bad();
 auto path=std::filesystem::u8path(relative);if(path.is_absolute())return bad();auto full=root;
 for(const auto& part:path){if(part==".."||part==".")return bad();full/=part;std::error_code ec;if(std::filesystem::is_symlink(std::filesystem::symlink_status(full,ec)))return bad();}
 return full;
}
Result<Pack> Pack::open_base(const std::filesystem::path& root,const SourceCatalog& catalog,const std::string& font_sha256){
 try {
  Pack out;out.root_=root;auto flc=safe_pack_path(root,"flc");if(!flc.has_value())return flc.error();
  for(const auto& [name,source]:catalog){
   auto folder=safe_pack_path(root,"flc/"+name);if(!folder.has_value())return folder.error();auto index=safe_pack_path(folder.value(),"index.json");if(!index.has_value())return index.error();
   std::error_code ec;auto bytes=std::filesystem::file_size(index.value(),ec);if(ec||bytes>32*1024*1024)return DataError{"",name,0,"missing or excessive pack manifest"};
   std::ifstream input(index.value());auto j=nlohmann::json::parse(input);
   auto fail=[&](std::string why)->Result<Pack>{return DataError{"",name,0,std::move(why)};};
   if(j.at("schema_version")!=1||j.at("resource").get<std::string>()!=name||j.at("source_sha256").get<std::string>()!=source.sha256||j.at("scale")!=3||j.at("logical_canvas")!=nlohmann::json{640,480}||j.at("physical_canvas")!=nlohmann::json{1920,1440}||j.at("frame_count")!=source.frames||j.at("frames").size()!=source.frames||j.at("atlas_gutter")!=2)return fail("pack source identity/count/canvas mismatch");
   std::map<std::string,std::pair<int,int>> atlases;
   for(const auto& a:j.at("atlases")){auto path=a.at("path").get<std::string>();if(std::filesystem::u8path(path).has_parent_path())return fail("atlas pages must use leaf filenames");auto safe=safe_pack_path(folder.value(),path);if(!safe.has_value())return safe.error();int w=a.at("size").at(0),h=a.at("size").at(1);if(w<5||h<5||w>4096||h>4096||!atlases.emplace(path,std::pair{w,h}).second)return fail("invalid atlas size or duplicate");if(a.at("sha256").get<std::string>().size()!=64)return fail("invalid atlas hash");out.atlas_paths_[resource_key(name)][path]=safe.value();}
   for(size_t i=0;i<source.frames;++i){const auto& f=j.at("frames").at(i);if(f.at("frame")!=i||f.at("resource").get<std::string>()!=name||f.at("indices_sha256").get<std::string>().empty()||f.at("palette_sha256").get<std::string>().empty())return fail("wrong frame identity or mask/palette association");
    auto delay=f.at("effective_delay");if(delay.at("denominator").get<int64_t>()<=0||delay.at("numerator").get<int64_t>()<0||(delay.at("unit")!="engine_updates"&&delay.at("unit")!="seconds"))return fail("invalid frame timing");
    for(const auto& [usage,u]:f.at("usages").items()){
     if(usage!="overlay"&&usage!="opaque_base")return fail("unknown usage");if(u.contains("pixel_sha256")&&!u.at("pixel_sha256").is_string())return fail("invalid frame pixel hash type");auto a=atlases.find(u.at("atlas").get<std::string>());if(a==atlases.end())return fail("unknown atlas association");
     int x=u.at("rect").at(0),y=u.at("rect").at(1),w=u.at("rect").at(2),h=u.at("rect").at(3);bool empty=u.at("empty");int ox=u.at("logical_offset").at(0),oy=u.at("logical_offset").at(1);
     if(x<2||y<2||w<1||h<1||int64_t(x)+w+2>a->second.first||int64_t(y)+h+2>a->second.second||ox<0||oy<0||ox>=640||oy>=480||(!empty&&(w%3||h%3||ox+w/3>640||oy+h/3>480)))return fail("invalid crop, offset or atlas gutter");
    }
    if(!f.at("usages").contains("overlay"))return fail("missing overlay usage");
   }
   if(!out.manifests_.emplace(resource_key(name),std::move(j)).second)return fail("duplicate resource identity");out.folders_[resource_key(name)]=folder.value();
  }
  if(!font_sha256.empty()){
   auto folder=safe_pack_path(root,"font");if(!folder.has_value())return folder.error();auto path=safe_pack_path(folder.value(),"index.json");if(!path.has_value())return path.error();
   std::error_code ec;auto bytes=std::filesystem::file_size(path.value(),ec);if(ec||bytes>1024*1024)return DataError{"","font",0,"missing or excessive font manifest"};
   std::ifstream input(path.value());auto j=nlohmann::json::parse(input);auto fail=[]()->Result<Pack>{return DataError{"","font",0,"invalid font identity, mapping or metrics"};};
   if(j.at("schema_version")!=1||j.at("source_sha256")!=font_sha256||j.at("glyph_count")!=256||j.at("glyphs").size()!=256||j.at("logical_advance")!=8||j.at("logical_baseline")!=12)return fail();
   auto atlas=safe_pack_path(folder.value(),"glyphs.png");if(!atlas.has_value())return atlas.error();auto hash=j.at("atlas").at("sha256").get<std::string>();if(hash.size()!=64)return fail();
   std::array<std::byte,4096> raw{};auto mapping=load_original_font({raw,0}).value();
   for(size_t i=0;i<256;++i){const auto& g=j.at("glyphs").at(i);if(g.at("index")!=i||g.at("codepoint").get<uint32_t>()!=uint32_t(mapping.glyphs[i].codepoint)||g.at("advance")!=24||g.at("baseline")!=36||g.at("output_pixel_sha256").get<std::string>().size()!=64)return fail();
    int x=g.at("rect").at(0),y=g.at("rect").at(1);if(x!=int(i%16)*28+2||y!=int(i/16)*52+2||g.at("rect").at(2)!=24||g.at("rect").at(3)!=48)return fail();
    auto& a=out.glyphs_[i];a.atlas=atlas.value();a.atlas_hash=hash;a.pixel_hash=g.at("output_pixel_sha256");a.atlas_width=448;a.atlas_height=832;a.u=x;a.v=y;a.width=24;a.height=48;
   }out.has_font_=true;
  }
  return out;
 }catch(const std::exception& e){return DataError{"","pack",0,std::string("invalid pack manifest: ")+e.what()};}
}
Result<Pack> Pack::open(const std::filesystem::path& root,const SourceCatalog& catalog,const std::string& font_sha256){
 auto base=open_base(root,catalog,font_sha256);if(!base.has_value())return base.error();
 auto index=safe_pack_path(root,"variants/index.json");if(!index.has_value())return index.error();if(!std::filesystem::exists(index.value()))return base;
 try {
  if(std::filesystem::file_size(index.value())>1024*1024)return DataError{"","variants",0,"excessive variant index"};std::ifstream input(index.value());auto j=nlohmann::json::parse(input);
  if(j.at("schema_version")!=1||!j.at("variants").is_array()||j.at("variants").size()>256)return DataError{"","variants",0,"invalid variant index"};
  for(const auto& v:j.at("variants")){
   auto palette=v.at("palette_sha256").get<std::string>(),path=v.at("path").get<std::string>();if(palette.size()!=64||!std::all_of(palette.begin(),palette.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||std::filesystem::u8path(path).has_parent_path())return DataError{"","variants",0,"invalid palette identity/path"};
   auto folder=safe_pack_path(root,"variants/"+path);if(!folder.has_value())return folder.error();SourceCatalog selected;
   for(const auto& r:v.at("resources")){auto name=r.get<std::string>();auto it=catalog.find(name);if(it==catalog.end()||!selected.emplace(*it).second)return DataError{"",name,0,"unknown or duplicate variant resource"};}
   auto variant=open_base(folder.value(),selected,{});if(!variant.has_value())return variant.error();
   for(auto& [name,manifest]:variant.value().manifests_){for(const auto& f:manifest.at("frames"))if(f.at("palette_sha256")!=palette)return DataError{"",name,0,"variant palette association mismatch"};auto key=name+"@"+palette;if(!base.value().manifests_.emplace(key,std::move(manifest)).second)return DataError{"",name,0,"duplicate variant association"};base.value().folders_[key]=variant.value().folders_.at(name);base.value().atlas_paths_[key]=std::move(variant.value().atlas_paths_.at(name));}
  }return base;
 }catch(const std::exception& e){return DataError{"","variants",0,std::string("invalid variant index: ")+e.what()};}
}
Result<FrameAsset> Pack::glyph(uint8_t index,GraphicsMode mode)const {
 if(!has_font_)return DataError{"","font",index,"missing Original glyph atlas"};auto out=glyphs_[index];out.owner=this;out.mode=mode;out.identity=std::to_string(int(mode))+"/font/"+std::to_string(index)+"/"+out.atlas_hash;return out;
}
Result<FrameAsset> Pack::resolve(const ResourceFrameKey& key,GraphicsMode mode)const {
 auto name=resource_key(key.resource);if(!key.palette.empty()&&manifests_.contains(name+"@"+key.palette))name+="@"+key.palette;auto it=manifests_.find(name);if(it==manifests_.end()||key.frame>=it->second.at("frames").size())return DataError{"",key.resource,key.frame,"missing pack frame"};
 const auto& f=it->second.at("frames").at(key.frame);if(!f.at("usages").contains(key.usage)||(!key.palette.empty()&&key.palette!=f.at("palette_sha256").get<std::string>()))return DataError{"",key.resource,key.frame,"missing usage or palette variant: "+key.resource+" frame "+std::to_string(key.frame)+" palette "+key.palette};
 const auto& u=f.at("usages").at(key.usage);auto path=u.at("atlas").get<std::string>();const auto& j=it->second;auto a=std::find_if(j.at("atlases").begin(),j.at("atlases").end(),[&](const auto& a){return a.at("path").template get<std::string>()==path;});
 FrameAsset out;out.owner=this;out.atlas=atlas_paths_.at(name).at(path);out.atlas_hash=a->at("sha256");out.pixel_hash=u.value("pixel_sha256","");out.mask_hash=f.at("indices_sha256");out.palette=f.at("palette_sha256");out.atlas_width=a->at("size").at(0);out.atlas_height=a->at("size").at(1);out.u=u.at("rect").at(0);out.v=u.at("rect").at(1);out.width=u.at("rect").at(2);out.height=u.at("rect").at(3);out.x=u.at("logical_offset").at(0).get<int>()*3;out.y=u.at("logical_offset").at(1).get<int>()*3;out.empty=u.at("empty");auto d=f.at("effective_delay");out.delay={d.at("numerator"),d.at("denominator"),d.at("unit")=="seconds"?DelayUnit::seconds:DelayUnit::engine_updates};out.mode=mode;
 out.identity=std::to_string(int(mode))+"/"+name+"/"+std::to_string(key.frame)+"/"+key.usage+"/"+out.palette+"/identity/"+out.atlas_hash;return out;
}
Result<std::vector<FrameAsset>> Pack::pages(const ResourceFrameKey& key,GraphicsMode mode)const {
 auto name=resource_key(key.resource);if(!key.palette.empty()&&manifests_.contains(name+"@"+key.palette))name+="@"+key.palette;
 auto it=manifests_.find(name);if(it==manifests_.end())return DataError{"",key.resource,0,"missing pack resource"};
 std::vector<FrameAsset> pages;
 for(const auto& f:it->second.at("frames")){
  if(!f.at("usages").contains(key.usage))continue;
  auto asset=resolve({key.resource,f.at("frame"),key.usage,f.at("palette_sha256")},mode);if(!asset.has_value())return asset.error();
  if(std::none_of(pages.begin(),pages.end(),[&](const auto& a){return a.atlas==asset.value().atlas;}))pages.push_back(std::move(asset.value()));
 }
 return pages;
}
Pack Pack::atlas_source(const FrameAsset& asset)const {
 // Retain only this page's validation metadata so a worker never borrows a Pack.
 Pack out;out.root_=root_;
 if(has_font_&&asset.atlas==glyphs_[0].atlas){out.has_font_=true;out.glyphs_=glyphs_;}
 for(const auto& [name,folder]:folders_)if(asset.atlas.parent_path()==folder){out.folders_[name]=folder;out.manifests_[name]=manifests_.at(name);}
 return out;
}
Result<AtlasImage> Pack::load_atlas(const FrameAsset& a)const {
 auto safe=safe_pack_path(root_,a.atlas.lexically_relative(root_).generic_string());if(!safe.has_value())return safe.error();
 auto hash=sha256_file(a.atlas);if(!hash.has_value())return hash.error();if(hash.value()!=a.atlas_hash)return DataError{"",a.atlas.filename().string(),0,"atlas hash mismatch"};
#ifdef _WIN32
 FILE* f=_wfopen(a.atlas.c_str(),L"rb");
#else
 FILE* f=std::fopen(a.atlas.c_str(),"rb");
#endif
 if(!f)return DataError{"","atlas",0,"cannot open atlas"};int w{},h{},c{};
 if(!stbi_info_from_file(f,&w,&h,&c)||w!=a.atlas_width||h!=a.atlas_height||c!=4){std::fclose(f);return DataError{"","atlas",0,"atlas dimensions or alpha mismatch"};}
 std::rewind(f);auto data=stbi_load_from_file(f,&w,&h,&c,4);std::fclose(f);if(!data)return DataError{"","atlas",0,"PNG decode failed"};AtlasImage out{w,h,{data,data+size_t(w)*h*4}};stbi_image_free(data);
 for(size_t i=3;i<out.pixels.size();i+=4)if(out.pixels[i]!=0&&out.pixels[i]!=255)return DataError{"","atlas",i,"non-binary atlas alpha"};
 if(has_font_&&a.atlas==glyphs_[0].atlas)for(const auto& g:glyphs_){
  std::vector<uint8_t> crop;for(int y=g.v;y<g.v+48;++y){auto first=out.pixels.data()+(size_t(y)*w+g.u)*4;crop.insert(crop.end(),first,first+24*4);}
  if(sha256(std::as_bytes(std::span(crop)))!=g.pixel_hash)return DataError{"","font",0,"glyph pixel hash mismatch"};
  for(size_t p=0;p<crop.size();p+=4)if((crop[p+3]&&!(crop[p]==255&&crop[p+1]==255&&crop[p+2]==255))||(!crop[p+3]&&(crop[p]||crop[p+1]||crop[p+2])))return DataError{"","font",0,"invalid glyph color"};
  for(int y=g.v-2;y<g.v+50;++y)for(int x=g.u-2;x<g.u+26;++x){auto px=&out.pixels[(size_t(y)*w+x)*4];auto edge=&out.pixels[(size_t(std::clamp(y,g.v,g.v+47))*w+std::clamp(x,g.u,g.u+23))*4];if(!std::equal(px,px+4,edge))return DataError{"","font",0,"glyph edge bleed"};}
 }
 for(const auto& [name,manifest]:manifests_)if(a.atlas.parent_path()==folders_.at(name))for(const auto& frame:manifest.at("frames"))for(const auto& u:frame.at("usages"))if(u.at("atlas").get<std::string>()==a.atlas.filename().string()){
  int x0=u.at("rect").at(0),y0=u.at("rect").at(1),cw=u.at("rect").at(2),ch=u.at("rect").at(3);std::vector<uint8_t> crop;crop.reserve(size_t(cw)*ch*4);
  bool empty=u.at("empty");
  for(int y=y0;y<y0+ch;++y){auto first=out.pixels.data()+(size_t(y)*w+x0)*4;crop.insert(crop.end(),first,first+size_t(cw)*4);if(empty)for(int x=0;x<cw;++x)if(first[x*4+3])return DataError{"","atlas",0,"empty frame has visible pixels"};}
  for(int y=y0-2;y<y0+ch+2;++y)for(int x=x0-2;x<x0+cw+2;++x){if(x>=x0&&x<x0+cw&&y>=y0&&y<y0+ch){x=x0+cw-1;continue;}int cx=std::clamp(x,x0,x0+cw-1),cy=std::clamp(y,y0,y0+ch-1);auto px=&out.pixels[(size_t(y)*w+x)*4];auto edge=&out.pixels[(size_t(cy)*w+cx)*4];if(!std::equal(px,px+4,edge))return DataError{"",name,0,"atlas edge bleed in "+a.atlas.filename().string()};}
  auto expected=u.value("pixel_sha256","");if(!expected.empty()&&sha256(std::as_bytes(std::span(crop)))!=expected)return DataError{"","atlas",0,"frame pixel hash mismatch"};
 }
 return out;
}
}
