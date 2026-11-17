#include "replay.h"
#include "debug/checkpoint.h"
#include <fstream>
#include <iostream>
#include <map>
int checkpoints_command(int argc,char** argv) {
 if(argc!=8||std::string_view(argv[1])!="compare")return 2;std::map<std::string,std::string> args;
 for(int i=2;i<argc;i+=2)if(!args.emplace(argv[i],argv[i+1]).second)return 2;
 if(!args.contains("--reference")||!args.contains("--native")||!args.contains("--report"))return 2;
 try {
  std::ifstream reference(args["--reference"]),native(args["--native"]);if(!reference||!native)return 1;
  auto compared=hl::compare_checkpoint(nlohmann::json::parse(reference),nlohmann::json::parse(native));
  if(!compared.has_value()){std::cerr<<compared.error().reason<<'\n';return 1;}
  auto report=compared.value();report["reference"]=args["--reference"];report["native"]=args["--native"];
  std::ofstream out(args["--report"]);out<<report.dump(2)<<'\n';return out&&report["match"].get<bool>()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
