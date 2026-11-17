#include "import.h"
#include "data/flac.h"
#include "data/hash.h"
#include <SDL3/SDL.h>
#include <fstream>
namespace hl { namespace {
std::string utf8_path(const std::filesystem::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
}
Result<void> encode_flac(std::span<const int16_t> samples,uint32_t rate,uint16_t channels,const std::filesystem::path& output){
 if(!rate||!channels||channels>2||samples.size()%channels)return DataError{"","FLAC",0,"invalid encoder input"};
#ifdef _WIN32
 auto encoder=std::filesystem::path("tools/audio/windows/flac.exe");
#else
 auto encoder=std::filesystem::path("tools/audio/macos/flac");
#endif
 auto raw=output;raw+=".stage.pcm";auto encoded=output;encoded+=".stage.flac";
 if(std::filesystem::exists(raw)||std::filesystem::exists(encoded))return DataError{"","FLAC",0,"stale encoder stage; previous output retained"};
 auto cleanup=[&]{std::error_code ec;std::filesystem::remove(raw,ec);std::filesystem::remove(encoded,ec);};
 std::ofstream f(raw,std::ios::binary);for(auto sample:samples){uint16_t v=static_cast<uint16_t>(sample);char bytes[]={char(v&255),char(v>>8)};f.write(bytes,2);}f.close();if(!f){cleanup();return DataError{"","FLAC",0,"PCM staging write failed"};}
 std::vector<std::string> args{utf8_path(encoder),"--silent","--verify","--force-raw-format","--endian=little","--sign=signed","--channels="+std::to_string(channels),"--bps=16","--sample-rate="+std::to_string(rate),"--no-padding","--no-seektable","--output-name="+utf8_path(encoded),utf8_path(raw)};std::vector<const char*> argv;for(auto& s:args)argv.push_back(s.c_str());argv.push_back(nullptr);
 auto* process=SDL_CreateProcess(argv.data(),false);if(!process){cleanup();return DataError{"","FLAC",0,"cannot start project-local pinned FLAC encoder"};}int exit=1;bool waited=SDL_WaitProcess(process,true,&exit);SDL_DestroyProcess(process);if(!waited||exit!=0){cleanup();return DataError{"","FLAC",0,"FLAC encoder failed"};}
 auto pcm=decode_flac(encoded);if(!pcm.has_value()){cleanup();return pcm.error();}if(pcm.value().rate!=rate||pcm.value().channels!=channels||pcm.value().samples.size()!=samples.size()||!std::equal(samples.begin(),samples.end(),pcm.value().samples.begin())){cleanup();return DataError{"","FLAC",0,"encoded FLAC differs from source PCM"};}
 std::error_code ec;std::filesystem::rename(encoded,output,ec);if(ec){cleanup();return DataError{"","FLAC",0,"cannot promote encoded audio; previous output retained"};}std::filesystem::remove(raw,ec);return {};
}
Result<nlohmann::json> rip_cd(const DiscLayout& d,const std::filesystem::path& output){
 auto ranges=cd_track_ranges(d);if(!ranges.has_value())return ranges.error();auto stage=output;stage+=".stage";if(std::filesystem::exists(stage)||std::filesystem::exists(output))return DataError{"","CD",0,"audio output/stage exists; previous audio retained"};std::filesystem::create_directories(stage);
 auto source_hash=sha256_file(d.bin);if(!source_hash.has_value()){std::filesystem::remove_all(stage);return source_hash.error();}
 nlohmann::json j{{"schema_version",1},{"rate",44100},{"channels",2},{"sample_format","signed16_little_endian"},{"audio_complete",false},{"tracks",nlohmann::json::array()},{"declared_track2_pregap_sectors",d.tracks[1].pregap},{"source_bin_sha256",source_hash.value()}};
 for(auto& range:ranges.value()){
  auto pcm=extract_cd_pcm(d,range);if(!pcm.has_value()){std::filesystem::remove_all(stage);return pcm.error();}std::string name="track"+std::to_string(range.track)+(range.pregap?"-pregap.flac":".flac");auto r=encode_flac(pcm.value(),44100,2,stage/name);if(!r.has_value()){std::filesystem::remove_all(stage);return r.error();}
  auto hash=sha256_file(stage/name);if(!hash.has_value()){std::filesystem::remove_all(stage);return hash.error();}
  j["tracks"].push_back({{"track",range.track},{"pregap",range.pregap},{"default_playback",!range.pregap},{"start_sector",range.begin},{"end_sector_exclusive",range.end},{"sample_frames",pcm.value().size()/2},{"path",name},{"source_pcm_sha256",sha256(std::as_bytes(std::span(pcm.value())))},{"flac_sha256",hash.value()},{"roundtrip_equal",true}});
 }
 j["audio_complete"]=true;std::ofstream f(stage/"manifest.json");f<<j.dump(2)<<'\n';f.close();if(!f){std::filesystem::remove_all(stage);return DataError{"","CD",0,"audio manifest write failed"};}std::error_code ec;std::filesystem::rename(stage,output,ec);if(ec){std::filesystem::remove_all(stage);return DataError{"","CD",0,"cannot promote audio directory"};}return j;
}
}
