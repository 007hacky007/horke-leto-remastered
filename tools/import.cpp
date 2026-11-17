#include "import.h"
#include "data/hash.h"
#include "data/flac.h"
#include <fstream>
#include <set>
#include <chrono>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ktmw32.h>
#endif
#ifdef __APPLE__
#include <stdio.h>
#endif
namespace hl { namespace {
std::string fold(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
}
Result<ImportManifest> import_game(const ImportRequest& req,const std::filesystem::path& destination){
 std::filesystem::path stage;
 try {
 if(req.rip_audio&&(req.cue.empty()||!req.cd_tracks[0].empty()||!req.cd_tracks[1].empty()))return DataError{"","import",0,"--rip-audio requires a CUE and cannot be combined with explicit tracks"};
 auto parent=std::filesystem::absolute(destination).parent_path();std::filesystem::create_directories(parent);
 if(std::filesystem::is_symlink(destination))return DataError{"","import",0,"symlink destination"};
 stage=parent/(destination.filename().string()+".stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 if(!std::filesystem::create_directory(stage))return DataError{"","import",0,"cannot reserve staging directory"};
 auto fail=[&](DataError e)->Result<ImportManifest>{std::filesystem::remove_all(stage);return e;};
 ImportManifest result;std::set<std::string> names;
 auto store=[&](const std::filesystem::path& name,ByteView bytes)->Result<void>{
  if(req.cancelled&&req.cancelled())return DataError{"","import",0,"import cancelled before promotion"};
  if(!safe_relative_path(name)||!names.insert(fold(name.generic_string())).second)return DataError{"","import",0,"unsafe or duplicate path"};
  auto path=stage/name;std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();if(!f)return DataError{"",name.generic_string(),0,"staging write failed"};
  auto hash=sha256_file(path);if(!hash.has_value())return hash.error();auto source=sha256(bytes);if(source!=hash.value())return DataError{"",name.generic_string(),0,"copy hash differs"};result.files.push_back({name.generic_string(),bytes.size(),source,hash.value()});return {};
 };
 if(!req.cue.empty()){
  auto disc=DiscReader::open(req.cue);if(!disc.has_value())return fail(disc.error());auto volume=read_iso_volume(disc.value());if(!volume.has_value())return fail(volume.error());result.volume=volume.value().name;
  auto root=read_iso_directory(disc.value(),volume.value().root);if(!root.has_value())return fail(root.error());std::optional<IsoExtent> hl_dir;for(auto& e:root.value())if(fold(e.name)=="hl"&&e.directory)hl_dir=e.extent;if(!hl_dir)return fail({"","ISO",0,"HL directory missing"});
  std::set<uint32_t> visited;std::function<Result<void>(IsoExtent,std::filesystem::path,unsigned)> copy;
  copy=[&](IsoExtent x,std::filesystem::path base,unsigned depth)->Result<void>{if(depth>32||!visited.insert(x.lba).second)return DataError{"","ISO",x.lba,"directory cycle or excessive depth"};auto entries=read_iso_directory(disc.value(),x);if(!entries.has_value())return entries.error();for(auto& e:entries.value()){auto name=base/e.name;if(e.directory){auto r=copy(e.extent,name,depth+1);if(!r.has_value())return r;}else{auto b=read_iso_extent(disc.value(),e.extent);if(!b.has_value())return b.error();auto r=store(name,b.value());if(!r.has_value())return r;}}return {};};
  auto r=copy(*hl_dir,{},0);if(!r.has_value())return fail(r.error());
 }else {
  if(req.directory.empty()||!std::filesystem::is_directory(req.directory))return fail({"","import",0,"source directory missing"});
  auto source=std::filesystem::canonical(req.directory);auto dest=std::filesystem::weakly_canonical(destination);auto rel=dest.lexically_relative(source);if(rel.empty()||rel=="."||safe_relative_path(rel))return fail({"","import",0,"destination must be outside source"});
  for(const auto& e:std::filesystem::recursive_directory_iterator(source)){
   if(e.is_symlink())return fail({"","import",0,"source symlink rejected"});if(e.is_directory())continue;if(!e.is_regular_file())return fail({"","import",0,"nonregular source"});
   std::ifstream f(e.path(),std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});if(!f||f.bad())return fail({"","import",0,"source read failed"});auto r=store(e.path().lexically_relative(source),std::as_bytes(std::span(b)));if(!r.has_value())return fail(r.error());
  }
 }
 for(auto name:{"data.adb","gfx.grp","hl.exe","jokes.chr","majer.chr"})if(!names.contains(name))return fail({"",name,0,"required file missing"});
 if(req.rip_audio){auto disc=DiscReader::open(req.cue);if(!disc.has_value())return fail(disc.error());auto ripped=rip_cd(disc.value().layout(),stage/"audio");if(!ripped.has_value())return fail(ripped.error());result.audio_complete=true;}
 if(req.cancelled&&req.cancelled())return fail({"","import",0,"import cancelled before promotion"});
 nlohmann::json audio{{"schema_version",1},{"audio_complete",true},{"tracks",nlohmann::json::array()}};
 if(!req.cd_tracks[0].empty()||!req.cd_tracks[1].empty()){
  if(!req.cue.empty()||req.cd_tracks[0].empty()||req.cd_tracks[1].empty())return fail({"","import",0,"both explicit CD tracks are required for directory import"});
  for(unsigned i=0;i<2;++i){auto pcm=decode_flac(req.cd_tracks[i]);if(!pcm.has_value())return fail(pcm.error());if(pcm.value().rate!=44100||pcm.value().channels!=2||!pcm.value().frames)return fail({"","import",0,"explicit CD track must be nonempty 44100 Hz stereo signed16 FLAC"});std::ifstream input(req.cd_tracks[i],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});if(!input)return fail({"","import",0,"cannot read explicit CD track"});auto name="track"+std::to_string(i+2)+".flac";auto copied=store(std::filesystem::path("audio")/name,std::as_bytes(std::span(bytes)));if(!copied.has_value())return fail(copied.error());audio["tracks"].push_back({{"track",i+2},{"pregap",false},{"default_playback",true},{"path",name},{"sample_frames",pcm.value().frames},{"source_pcm_sha256",sha256(std::as_bytes(std::span(pcm.value().samples)))}});}
  std::ofstream audio_file(stage/"audio/manifest.json");audio_file<<audio.dump(2)<<'\n';audio_file.close();if(!audio_file)return fail({"","import",0,"audio manifest write failed"});result.audio_complete=true;
 }
 nlohmann::json manifest{{"schema_version",1},{"volume",result.volume},{"audio_complete",result.audio_complete},{"audio_status",result.audio_complete?"complete":"incomplete: CD tracks absent"},{"files",nlohmann::json::array()}};
 if(result.audio_complete)manifest["audio_manifest"]="audio/manifest.json";
 for(auto& f:result.files)manifest["files"].push_back({{"name",f.name},{"size",f.size},{"source_sha256",f.source_sha256},{"destination_sha256",f.destination_sha256}});
 std::ofstream out(stage/"import-manifest.json");out<<manifest.dump(2)<<'\n';out.close();if(!out)return fail({"","import",0,"manifest write failed"});
 if(std::filesystem::exists(destination)){
#ifdef __APPLE__
  if(renamex_np(stage.c_str(),destination.c_str(),RENAME_SWAP)!=0)return fail({"","import",0,"atomic directory swap failed"});std::filesystem::remove_all(stage);
#elif defined(_WIN32)
  auto target=std::filesystem::absolute(destination);auto backup=stage;backup+=L".previous";
  HANDLE tx=CreateTransaction(nullptr,nullptr,0,0,0,0,const_cast<wchar_t*>(L"Local asset import"));
  if(tx==INVALID_HANDLE_VALUE)return fail({"","import",0,"cannot create atomic import transaction"});
  bool moved=MoveFileTransactedW(target.c_str(),backup.c_str(),nullptr,nullptr,0,tx)&&MoveFileTransactedW(stage.c_str(),target.c_str(),nullptr,nullptr,0,tx);
  bool committed=moved&&CommitTransaction(tx);auto code=GetLastError();if(!committed)RollbackTransaction(tx);CloseHandle(tx);
  if(!committed)return fail({"","import",0,"atomic import transaction failed: "+std::to_string(code)});
  std::error_code cleanup;std::filesystem::remove_all(backup,cleanup);
#else
  return fail({"","import",0,"atomic directory promotion unsupported on this platform"});
#endif
 }else std::filesystem::rename(stage,destination);
 return result;
 }catch(const std::filesystem::filesystem_error&){std::error_code ec;if(!stage.empty())std::filesystem::remove_all(stage,ec);return DataError{"","import",0,"filesystem operation failed; previous import retained"};}
}
}
