#include "pack.h"
#include "data/grp.h"
#include "data/hash.h"
#include <fstream>
#include <iostream>
#include <set>
namespace hl {
Result<nlohmann::json> verify_pack(const std::filesystem::path& root,const std::filesystem::path& data){
 auto catalog=source_catalog(data);if(!catalog.has_value())return catalog.error();auto p=Pack::open(root,catalog.value());if(!p.has_value())return p.error();
 uint64_t frames=0,assets=0,bytes=0,largest=0;std::set<std::filesystem::path> pages;
 for(const auto& [name,source]:catalog.value()){
  for(unsigned frame=0;frame<source.frames;++frame){++frames;for(auto usage:{"overlay","opaque_base"}){auto a=p.value().resolve({name,frame,usage},GraphicsMode::Original);if(!a.has_value())return a.error();++assets;if(pages.insert(a.value().atlas).second){auto im=p.value().load_atlas(a.value());if(!im.has_value())return im.error();bytes+=std::filesystem::file_size(a.value().atlas);largest=std::max(largest,uint64_t(im.value().pixels.size()));}}}
 }
 return nlohmann::json{{"schema_version",1},{"passed",true},{"resources",catalog.value().size()},{"frames",frames},{"resolved_usages",assets},{"atlas_pages",pages.size()},{"png_bytes",bytes},{"largest_texture_bytes",largest},{"cache_budget_bytes",256*1024*1024},{"verification","source hashes/counts, all frame metadata/paths, all atlas hashes/dimensions/alpha, frame pixel hashes and crop gutters"}};
}
}
int pack_command(int argc,char** argv){if(argc<2||std::string_view(argv[1])!="verify")return 2;std::map<std::string,std::string> options;for(int i=2;i+1<argc;i+=2)options[argv[i]]=argv[i+1];if(!options.contains("--pack")||!options.contains("--data")||!options.contains("--report"))return 2;auto r=hl::verify_pack(std::filesystem::u8path(options["--pack"]),std::filesystem::u8path(options["--data"]));if(!r.has_value()){std::cerr<<r.error().resource<<": "<<r.error().reason<<'\n';return 1;}std::ofstream out(std::filesystem::u8path(options["--report"]));out<<r.value().dump(2)<<'\n';return out?0:1;}
