#include "launch_config.h"
#include "data/flac.h"
#include "data/hash.h"
#include "data/grp.h"
#include "render/pack.h"
#include <chrono>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace hl::delivery {
std::string utf8(const fs::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
namespace {
DataError error(std::string why){return {"","launcher",0,std::move(why)};}
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
Result<void> audio_complete(const fs::path& data){
 for(auto name:{"track2.flac","track3.flac"}){
  auto pcm=decode_flac(data/"audio"/name);
  if(!pcm.has_value())return error(std::string("Missing or corrupt CD audio: ")+name);
  if(pcm.value().rate!=44100||pcm.value().channels!=2||!pcm.value().frames)return error("CD tracks must be nonempty 44100 Hz stereo FLAC");
 }
 return {};
}
}
Result<ImportRequest> import_source(const fs::path& source){
 try {
  ImportRequest req;
  if(fs::is_directory(source)){req.directory=fs::is_directory(source/"HL")?source/"HL":source;return req;}
  if(!fs::is_regular_file(source))return error("Game source does not exist");
  auto extension=lower(utf8(source.extension()));
  if(extension==".cue"){req.cue=source;req.rip_audio=true;return req;}
  if(extension==".bin"){
   for(const auto& e:fs::directory_iterator(source.parent_path()))
    if(lower(utf8(e.path().stem()))==lower(utf8(source.stem()))&&lower(utf8(e.path().extension()))==".cue"){req.cue=e.path();req.rip_audio=true;return req;}
   return error("Select the companion CUE; a BIN alone has no CD track boundaries");
  }
  return error("Choose a BIN/CUE or an extracted HL folder");
 }catch(const fs::filesystem_error&){return error("Cannot read game source");}
}
Result<Paths> load_paths(const fs::path& file){
 try {
  if(fs::file_size(file)>16384)return error("Launch settings are too large");
  std::ifstream in(file);auto j=nlohmann::json::parse(in);
  if(j.at("schema_version")!=1)return error("Unsupported launch settings version");
  Paths out{fs::u8path(j.at("data").get<std::string>()),fs::u8path(j.at("packs").get<std::string>())};
  if(out.data.empty()||out.packs.empty())return error("Empty data or packs path");
  if(out.data.is_relative())out.data=file.parent_path()/out.data;
  if(out.packs.is_relative())out.packs=file.parent_path()/out.packs;
  return out;
 }catch(const std::exception&){return error("Cannot read launch.json; remove or repair that file before retrying");}
}
Result<void> save_paths(const fs::path& file,const Paths& paths){
 try {
  fs::create_directories(file.parent_path());auto tmp=file;tmp+=".tmp";
  std::ofstream out(tmp);out<<nlohmann::json{{"schema_version",1},{"data",utf8(paths.data)},{"packs",utf8(paths.packs)}}.dump(2)<<'\n';out.close();
  if(!out)return error("Cannot write launch settings; previous settings retained");
#ifdef _WIN32
  if(!MoveFileExW(tmp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return error("Cannot replace launch settings");
#else
  fs::rename(tmp,file);
#endif
  return {};
 }catch(const fs::filesystem_error&){return error("Cannot save launch settings; previous settings retained");}
}
Result<void> validate_pack_files(const fs::path& root,bool hashes){
 try {
  if(!fs::is_directory(root)||fs::is_symlink(root))return error("Pack folder missing or symlinked");
  size_t count=0;
  for(const auto& entry:fs::recursive_directory_iterator(root)){
   if(entry.is_symlink())return error("Pack symlinks are not supported");
   if(entry.path().filename()!="index.json")continue;
   if(fs::file_size(entry.path())>32*1024*1024)return error("Excessive pack index");
   std::ifstream in(entry.path());auto j=nlohmann::json::parse(in);
   auto check=[&](const std::string& name,const std::string& hash,int width,int height)->Result<void>{
    auto safe=safe_pack_path(entry.path().parent_path(),name);if(!safe.has_value())return safe.error();
    std::ifstream image(safe.value(),std::ios::binary);std::array<unsigned char,26> header{};image.read(reinterpret_cast<char*>(header.data()),header.size());
    auto u32=[&](size_t offset){return (uint32_t(header[offset])<<24)|(uint32_t(header[offset+1])<<16)|(uint32_t(header[offset+2])<<8)|header[offset+3];};
    const std::array<unsigned char,8> signature{137,80,78,71,13,10,26,10};
    if(!image||!std::equal(signature.begin(),signature.end(),header.begin())||u32(16)!=uint32_t(width)||u32(20)!=uint32_t(height)||header[24]!=8||header[25]!=6)return error("Missing or invalid RGBA PNG atlas: "+name);
    if(hashes){auto actual=sha256_file(safe.value());if(!actual.has_value()||actual.value()!=hash)return error("Pack atlas hash mismatch: "+name);}
    ++count;return {};
   };
   if(j.contains("atlases"))for(const auto& a:j.at("atlases")){auto valid=check(a.at("path"),a.at("sha256"),a.at("size").at(0),a.at("size").at(1));if(!valid.has_value())return valid;}
   if(j.contains("glyph_count")){auto valid=check("glyphs.png",j.at("atlas").at("sha256"),448,832);if(!valid.has_value())return valid;}
  }
  if(!count)return error("Pack contains no atlas payloads");return {};
 }catch(const std::exception&){return error("Cannot validate pack payloads");}
}
Result<void> validate_paths(const Paths& paths,bool hashes){
 auto audio=audio_complete(paths.data);if(!audio.has_value())return audio;
 auto catalog=source_catalog(paths.data);if(!catalog.has_value())return catalog.error();
 auto exe=read_file(paths.data/"HL.EXE");
 if(!exe.has_value()||exe.value().size()<252284+4096)return error("Missing original HL.EXE font");
 auto original=Pack::open(paths.packs/"original",catalog.value(),sha256(ByteView(exe.value()).subspan(252284,4096)));
 if(!original.has_value())return original.error();
 SourceCatalog replacements;
 for(const auto& [name,source]:catalog.value())if(fs::exists(paths.packs/"remastered"/"flc"/name/"index.json"))replacements.emplace(name,source);
 if(replacements.empty())return error("Remastered pack is missing or empty");
 auto remastered=Pack::open(paths.packs/"remastered",replacements);
 if(!remastered.has_value())return remastered.error();
 for(const auto& name:{"original","remastered"}){auto valid=validate_pack_files(paths.packs/name,hashes);if(!valid.has_value())return valid;}
 return {};
}
Result<ImportManifest> stage_import(ImportRequest req,const fs::path& destination){
 try {
  if(fs::exists(destination))return error("Staged import requires a new destination");
  auto tracks=req.cd_tracks;
  if(!tracks[0].empty()||!tracks[1].empty()){
   if(req.directory.empty()||tracks[0].empty()||tracks[1].empty())return error("Both explicit CD tracks require a folder source");
   req.cd_tracks={};
  }
  auto imported=import_game(req,destination);if(!imported.has_value())return imported.error();
  if(!tracks[0].empty()){
   fs::create_directories(destination/"audio");
   for(size_t i=0;i<2;++i)fs::copy_file(tracks[i],destination/"audio"/("track"+std::to_string(i+2)+".flac"),fs::copy_options::overwrite_existing);
  }
  return imported;
 }catch(const fs::filesystem_error&){std::error_code ec;fs::remove_all(destination,ec);return error("Cannot stage imported CD audio");}
}
Result<void> complete_import_manifest(const fs::path& destination){
 try {
  nlohmann::json audio{{"schema_version",1},{"audio_complete",true},{"rate",44100},{"channels",2},{"tracks",nlohmann::json::array()}};
  for(unsigned i=2;i<=3;++i){
   auto name="track"+std::to_string(i)+".flac";auto pcm=decode_flac(destination/"audio"/name);
   if(!pcm.has_value()||pcm.value().rate!=44100||pcm.value().channels!=2||!pcm.value().frames)return error("Cannot complete missing or corrupt CD audio");
   audio["tracks"].push_back({{"track",i},{"pregap",false},{"default_playback",true},{"path",name},{"sample_frames",pcm.value().frames},{"source_pcm_sha256",sha256(std::as_bytes(std::span(pcm.value().samples)))},{"flac_sha256",sha256_file(destination/"audio"/name).value()}});
  }
  std::ofstream audio_file(destination/"audio/manifest.json");audio_file<<audio.dump(2)<<'\n';audio_file.close();if(!audio_file)return error("Cannot write audio manifest");
  std::ifstream in(destination/"import-manifest.json");auto j=nlohmann::json::parse(in);in.close();
  auto& files=j.at("files");
  files.erase(std::remove_if(files.begin(),files.end(),[](const auto& f){auto name=f.at("name").template get<std::string>();return name=="import-manifest.json"||name=="audio/manifest.json"||name=="audio/track2.flac"||name=="audio/track3.flac";}),files.end());
  for(auto name:{"audio/track2.flac","audio/track3.flac","audio/manifest.json"}){
   auto hash=sha256_file(destination/name);if(!hash.has_value())return hash.error();
   files.push_back({{"name",name},{"size",fs::file_size(destination/name)},{"source_sha256",hash.value()},{"destination_sha256",hash.value()}});
  }
  j["audio_complete"]=true;j["audio_status"]="complete";j["audio_manifest"]="audio/manifest.json";
  std::ofstream out(destination/"import-manifest.json");out<<j.dump(2)<<'\n';out.close();if(!out)return error("Cannot write import manifest");
  return {};
 }catch(const std::exception&){return error("Cannot complete import metadata");}
}
Result<Paths> install_data(ImportRequest req,const fs::path& profile,const fs::path& packs){
 auto destination=profile/("data-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto imported=stage_import(req,destination);if(!imported.has_value())return imported.error();
 Paths paths{destination,packs};
 auto valid=validate_paths(paths,true);
 if(!valid.has_value()){fs::remove_all(destination);return valid.error();}
 auto complete=complete_import_manifest(destination);if(!complete.has_value()){fs::remove_all(destination);return complete.error();}
 auto saved=save_paths(profile/"launch.json",paths);
 if(!saved.has_value()){fs::remove_all(destination);return saved.error();}
 return paths;
}
}
