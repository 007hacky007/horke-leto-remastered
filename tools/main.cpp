#include "le/le_to_elf.h"
#include "pack.h"
#include "benchmark.h"
#include "import.h"
#include "assets.h"
#include "viewer.h"
#include "script_trace.h"
#include "replay.h"
#include "data/hash.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc,char** argv) {
 if(argc>1&&std::string_view(argv[1])=="benchmark")return benchmark_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="pack")return pack_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="checkpoints")return checkpoints_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="replay-trace")return replay_trace_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="scripts")return scripts_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="view")return viewer_command(argc-1,argv+1);
 if(argc>1&&std::string_view(argv[1])=="assets")return assets_command(argc-1,argv+1);
 if(argc>1 && std::string_view(argv[1])=="import") {
  std::map<std::string,std::string> opts;
  for(int i=2;i<argc;++i){if(std::string_view(argv[i])=="--rip-audio"){opts[argv[i]]="true";continue;}if(i+1>=argc||!opts.emplace(argv[i],argv[i+1]).second)return 2;++i;}
  if(!opts.contains("--out")||!opts.contains("--report")||opts.contains("--cue")==opts.contains("--directory"))return 2;
  hl::ImportRequest request{opts["--cue"],opts["--directory"]};request.cd_tracks={opts["--track2"],opts["--track3"]};request.rip_audio=opts.contains("--rip-audio");
  auto r=hl::import_game(request,opts["--out"]);
  if(!r.has_value()){std::cerr<<r.error().resource<<":"<<r.error().byte_offset<<": "<<r.error().reason<<'\n';return 1;}
  nlohmann::json j{{"schema_version",1},{"volume",r.value().volume},{"audio_complete",r.value().audio_complete},{"files",nlohmann::json::array()}};
  for(auto& f:r.value().files)j["files"].push_back({{"name",f.name},{"size",f.size},{"source_sha256",f.source_sha256},{"destination_sha256",f.destination_sha256}});
  if(request.rip_audio){std::ifstream audio_file(std::filesystem::path(opts["--out"])/"audio/manifest.json");j["audio"]=nlohmann::json::parse(audio_file);}
  std::ofstream out(opts["--report"]);out<<j.dump(2)<<'\n';return out?0:1;
 }
 if(argc!=8 || std::string_view(argv[1])!="le-convert") {std::cerr<<"usage: hl-tool le-convert --input PATH --output PATH --report PATH\n";return 2;}
 std::map<std::string,std::string> args;
 for(int i=2;i<argc;i+=2) {if(!args.emplace(argv[i],argv[i+1]).second)return 2;}
 if(!args.contains("--input") || !args.contains("--output") || !args.contains("--report"))return 2;
 if(std::filesystem::exists(args["--output"]) && std::filesystem::equivalent(args["--input"],args["--output"]))return 2;
 std::ifstream f(args["--input"],std::ios::binary);if(!f)return 1;
 std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
 auto image=hl::parse_le(std::as_bytes(std::span(bytes)));if(!image.has_value()){std::cerr<<image.error().reason<<'\n';return 1;}
 auto relocated=hl::apply_fixups(image.value());if(!relocated.has_value()){std::cerr<<relocated.error().reason<<'\n';return 1;}
 auto written=hl::write_elf(relocated.value(),args["--output"]);if(!written.has_value()){std::cerr<<written.error().reason<<'\n';return 1;}
 nlohmann::json report{{"schema_version",1},{"entry",image.value().entry},{"pages",image.value().page_count},{"page_size",image.value().page_size},{"objects",nlohmann::json::array()},{"fixups",nlohmann::json::array()}};
 for(auto& o:image.value().objects)report["objects"].push_back({{"base",o.base},{"virtual_size",o.bytes.size()},{"first_page",o.first_page},{"page_count",o.page_count},{"flags",o.flags}});
 for(auto& x:relocated.value().fixups)report["fixups"].push_back({{"page",x.page},{"source_object",x.source_object},{"source_offset",x.source_offset},{"target_object",x.target_object},{"target_offset",x.target_offset},{"value",x.value},{"flags",x.flags}});
 std::ofstream out(args["--report"]);out<<report.dump(2)<<'\n';return out?0:1;
}
