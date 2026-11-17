#include "coverage.h"
#include "assets.h"
#include "image_io.h"
#include "data/grp.h"
#include "data/adb.h"
#include "data/disc.h"
#include "data/wav.h"
#include "data/text.h"
#include "data/flac.h"
#include "data/hash.h"
#include <fstream>
namespace hl {
Result<nlohmann::json> validate_coverage(const CoverageState& s){
 auto fail=[](std::string reason)->Result<nlohmann::json>{return DataError{"","coverage",0,std::move(reason)};};
 if(s.expected.empty()||s.decoded.size()!=s.expected.size()||s.exported.size()!=s.expected.size())return fail("FLC identity set differs");size_t frames=0;
 for(auto& [name,count]:s.expected){auto d=s.decoded.find(name),e=s.exported.find(name);if(d==s.decoded.end()||e==s.exported.end()||d->second.size()!=count||e->second!=d->second)return fail("decoded/exported frame set differs for "+name);for(uint32_t i=0;i<count;++i)if(!d->second.contains(i))return fail("missing frame index for "+name);frames+=count;}
 if(s.wav_errors||s.wav_expected.empty()||s.wav_expected!=s.wav_decoded)return fail("WAV decode set differs or contains errors");if(s.audio_tracks!=std::set<unsigned>{2,3})return fail("CD tracks incomplete");if(!s.text_font_valid||!s.transparency_valid)return fail("text/font or transparency evidence missing");
 if(s.exception_names!=std::set<std::string>{"IN-PR-MO.FLC","IN-PRE-M.FLC","MAPA7-BL.FLC"})return fail("depth-zero evidence incomplete");return nlohmann::json{{"schema_version",1},{"passed",true},{"flc_files",s.expected.size()},{"visible_frames",frames},{"wav_files",s.wav_decoded.size()},{"cd_tracks",2},{"errors",nlohmann::json::array()}};
}
namespace {nlohmann::json read_json(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("required report missing: "+p.generic_string());return nlohmann::json::parse(f);}}
Result<nlohmann::json> verify_assets(const std::filesystem::path& data,const std::filesystem::path& frames){
 try {
  auto archive=GrpArchive::open(data/"GFX.GRP");if(!archive.has_value())return archive.error();auto adb=AdbDatabase::open(data/"DATA.ADB");if(!adb.has_value())return adb.error();CoverageState state;
  auto flc=read_json("work/m1/flc.json"),wav=read_json("work/m1/wav.json"),exceptions=read_json("analysis/evidence/flc-exceptions.json"),text=read_json("analysis/evidence/text-font.json"),transparency=read_json("analysis/evidence/transparency.json");
  if(!flc.at("errors").empty())return DataError{"","coverage",0,"FLC decode errors"};
  std::map<std::string,nlohmann::json> decoded;for(auto& f:flc.at("resources"))if(!decoded.emplace(f.at("name").get<std::string>(),f).second)return DataError{"","coverage",0,"duplicate FLC report identity"};
  for(auto& e:archive.value().entries()){
   auto key=ascii_key(e.name);if(key.ends_with(".wav")){state.wav_expected.insert(e.name);continue;}if(!key.ends_with(".flc"))continue;
   auto source=archive.value().read(e.name);if(!source.has_value())return source.error();auto d=FlcDecoder::open(source.value(),e.name);if(!d.has_value())return d.error();state.expected[e.name]=d.value().declared_frames();auto it=decoded.find(e.name);if(it==decoded.end()||it->second.at("source_sha256")!=sha256(source.value()))return DataError{"",e.name,0,"decoder source hash differs"};
   auto manifest=read_json(frames/(e.name+".json"));if(manifest.at("source_sha256")!=it->second.at("source_sha256"))return DataError{"",e.name,0,"export source hash differs"};
   std::map<uint32_t,std::string> hashes;for(auto& f:it->second.at("frames")){auto index=f.at("index").get<uint32_t>();if(!state.decoded[e.name].insert(index).second)return DataError{"",e.name,index,"duplicate decoded frame"};hashes[index]=f.at("indices_sha256");if(f.at("effective_delay").at("unit")!="engine_updates")return DataError{"",e.name,index,"unresolved timing units"};}
   for(auto& f:manifest.at("frames")){
    auto index=f.at("index").get<uint32_t>();if(!state.exported[e.name].insert(index).second)return DataError{"",e.name,index,"duplicate exported frame"};if(f.at("palette").size()!=256||!f.at("roundtrip_equal").get<bool>())return DataError{"",e.name,index,"palette or roundtrip invalid"};
    auto native=d.value().next();if(!native.has_value())return native.error();if(!native.value())return DataError{"",e.name,index,"export contains extra visible frame"};
    std::vector<uint8_t> rgba;auto box=f.at("crop");CroppedFrame crop{box.at("x"),box.at("y"),box.at("width"),box.at("height"),box.at("empty"),{}, {}};
    if(!crop.empty)for(auto [suffix,channels]:{std::pair{".png",4u},std::pair{".indices.png",1u},std::pair{".mask.png",1u}}){std::filesystem::path path=f.at(suffix).at("path").get<std::string>();if(!safe_relative_path(path))return DataError{"",e.name,index,"escaping frame path"};auto full=frames/e.name/path;auto hash=sha256_file(full);if(!hash.has_value()||hash.value()!=f.at(suffix).at("sha256").get<std::string>())return DataError{"",e.name,index,"PNG hash differs/missing"};auto image=read_png(full,channels);if(!image.has_value())return image.error();if(image.value().width!=crop.width||image.value().height!=crop.height)return DataError{"",e.name,index,"PNG dimensions differ"};if(channels==4)rgba=std::move(image.value().pixels);if(std::string_view(suffix)==".indices.png")crop.indices=std::move(image.value().pixels);if(std::string_view(suffix)==".mask.png")crop.mask=std::move(image.value().pixels);}
    auto validated=validate_frame_export(*native.value(),f,manifest.at("usage_rules"),crop,rgba,{manifest.at("transparent_index").get<uint8_t>(),manifest.at("provenance").get<std::string>()});if(!validated.has_value())return validated.error();
    auto restored=restore_frame(crop,f.at("logical_width"),f.at("logical_height"),manifest.at("transparent_index").is_null()?0:manifest.at("transparent_index").get<uint8_t>());if(!restored.has_value())return restored.error();if(!hashes.contains(index)||sha256(std::as_bytes(std::span(restored.value())))!=hashes[index]||f.at("indices_sha256")!=hashes[index])return DataError{"",e.name,index,"restored frame differs from decode"};
   }
  }
  for(auto& e:wav.at("resources")){auto name=e.at("name").get<std::string>();if(!state.wav_decoded.insert(name).second)return DataError{"",name,0,"duplicate WAV report identity"};auto b=archive.value().read(name);if(!b.has_value())return b.error();if(sha256(b.value())!=e.at("source_sha256").get<std::string>())return DataError{"",name,0,"WAV source hash differs"};auto p=decode_wav(b.value());if(!p.has_value())return p.error();if(p.value().frames!=e.at("sample_frames")||sha256(std::as_bytes(std::span(p.value().samples)))!=e.at("pcm_sha256").get<std::string>())return DataError{"",name,0,"WAV PCM differs"};}
  auto audio=read_json(data/"audio/manifest.json");if(!audio.at("audio_complete").get<bool>())return DataError{"","coverage",0,"CD import incomplete"};for(auto& t:audio.at("tracks")){std::filesystem::path name=t.at("path").get<std::string>();if(!safe_relative_path(name))return DataError{"","coverage",0,"unsafe CD path"};auto pcm=decode_flac(data/"audio"/name);if(!pcm.has_value())return pcm.error();if(pcm.value().frames!=t.at("sample_frames")||sha256(std::as_bytes(std::span(pcm.value().samples)))!=t.at("source_pcm_sha256").get<std::string>())return DataError{"","coverage",0,"CD roundtrip differs"};if(!t.at("pregap").get<bool>())state.audio_tracks.insert(t.at("track").get<unsigned>());}
  if(exceptions.at("passed").get<bool>())for(auto& e:exceptions.at("resources"))if(e.at("native_and_vm_index_samples_equal").get<bool>()&&e.at("all_declared_frames_observed").get<bool>())state.exception_names.insert(e.at("name").get<std::string>());state.text_font_valid=text.at("passed").get<bool>();state.transparency_valid=validate_transparency_evidence(transparency).has_value();
  auto sources=read_json("analysis/evidence/audio-sources.json");if(!sources.at("passed").get<bool>())return DataError{"","coverage",0,"audio-source evidence incomplete"};
  auto imported=read_json(data/"import-manifest.json");for(auto& file:imported.at("files")){std::filesystem::path name=file.at("name").get<std::string>();if(!safe_relative_path(name))return DataError{"","coverage",0,"unsafe imported path"};auto hash=sha256_file(data/name);if(!hash.has_value()||hash.value()!=file.at("destination_sha256").get<std::string>())return DataError{"",name.generic_string(),0,"import hash changed"};}
  auto exe=read_file(data/"HL.EXE");if(!exe.has_value())return exe.error();if(sha256(exe.value())!=text.at("font").at("source_sha256").get<std::string>()||exe.value().size()<256380||sha256(ByteView(exe.value()).subspan(252284,4096))!=text.at("font").at("sha256").get<std::string>())return DataError{"","coverage",0,"font source changed"};
  for(auto& known:text.at("known_strings")){auto stored=adb.value().read(known.at("entry").get<std::string>());if(!stored.has_value())return stored.error();auto plain=decrypt_game_text(stored.value());auto decoded_text=decode_game_text(plain,GameEncoding::kamenicky);auto expected=known.at("kamenicky").get<std::string>();expected.erase(std::remove(expected.begin(),expected.end(),'\r'),expected.end());if(!decoded_text.has_value()||decoded_text.value()!=expected)return DataError{"","coverage",0,"known Czech text differs"};}
  size_t candidates=0,references=0;for(auto& e:adb.value().entries()){auto b=adb.value().read(e.name);if(!b.has_value())return b.error();auto obj=inspect_object_header(b.value());if(obj.has_value()){++candidates;references+=obj.value().resources.size();}}
  if(archive.value().entries().size()!=2298||adb.value().entries().size()!=6115||candidates!=833||references!=8816)return DataError{"","coverage",0,"M1 archive/database counters differ"};
  for(auto platform:{"macos","windows"}){auto visit=read_json(std::string("work/m1/viewer-")+platform+".json");if(!visit.at("passed").get<bool>())return DataError{"","coverage",0,"viewer sweep did not pass"};std::map<std::string,std::set<uint32_t>> visited;for(auto& row:visit.at("visited")){auto name=row.at("resource").get<std::string>();auto frame=row.at("frame").get<uint32_t>();if(!visited[name].insert(frame).second)return DataError{"",name,frame,"duplicate viewer visitation"};}if(visited!=state.decoded)return DataError{"","coverage",0,"viewer visitation differs from decoded set"};}
  auto result=validate_coverage(state);if(!result.has_value())return result.error();auto j=result.value();if(j["flc_files"]!=595||j["visible_frames"]!=4903||j["wav_files"]!=1703)return DataError{"","coverage",0,"M1 asset totals differ"};j["object_candidates"]=candidates;j["resource_references"]=references;j["grp_entries"]=archive.value().entries().size();j["adb_entries"]=adb.value().entries().size();j["source_grp_sha256"]=sha256_file(data/"GFX.GRP").value();j["source_adb_sha256"]=sha256_file(data/"DATA.ADB").value();j["frame_export_index"]=frames.generic_string()+"/index.json";j["timing_unit"]="engine_updates";return j;
 }catch(const std::exception& e){return DataError{"","coverage",0,e.what()};}
}
}
namespace hl {
Result<void> validate_transparency_evidence(const nlohmann::json& j){try{if(j.at("overlay_index")!=0||j.at("opaque_base_rule").get<std::string>().empty()||j.at("opaque_base_usage").at("condition")!="first_full_screen_base"||j.at("opaque_base_usage").at("mode")!="opaque_copy"||!j.at("runtime_probe").at("match").get<bool>())return DataError{"","transparency",0,"incomplete overlay/opaque-base evidence"};return {};}catch(const std::exception&){return DataError{"","transparency",0,"missing overlay/opaque-base evidence"};}}
Result<void> validate_frame_export(const IndexedFrame& expected,const nlohmann::json& frame,const nlohmann::json& usage,const CroppedFrame& crop,std::span<const uint8_t> rgba,const TransparencyRule& rule){
 auto fail=[&](std::string why)->Result<void>{return DataError{"","frame",expected.frame_index,std::move(why)};};
 try {
  if(frame.at("index")!=expected.frame_index||frame.at("logical_width")!=expected.logical_width||frame.at("logical_height")!=expected.logical_height||frame.at("palette")!=nlohmann::json(expected.palette))return fail("frame identity, dimensions, or palette differs");
  nlohmann::json declared{{"numerator",expected.declared_delay.numerator},{"denominator",expected.declared_delay.denominator},{"unit","seconds"}},effective{{"numerator",expected.effective_delay.numerator},{"denominator",expected.effective_delay.denominator},{"unit","engine_updates"}};
  if(frame.at("declared_delay")!=declared||frame.at("effective_delay")!=effective)return fail("frame timing differs");
  if(usage.at("flc_type_0_overlay")!="index 0 transparent"||usage.at("first_full_screen_base")!="opaque copy of complete restored indices; palette index 0 remains opaque"||!rule.index||*rule.index!=0)return fail("missing/invalid overlay or opaque-base usage");
  if(frame.at("transparency_provenance")!=rule.provenance)return fail("transparency provenance differs");
  auto want=crop_frame(expected,rule);if(!want.has_value())return want.error();auto& w=want.value();nlohmann::json box{{"x",w.x},{"y",w.y},{"width",w.width},{"height",w.height},{"empty",w.empty}};
  if(frame.at("crop")!=box||crop.x!=w.x||crop.y!=w.y||crop.width!=w.width||crop.height!=w.height||crop.empty!=w.empty||crop.indices!=w.indices||crop.mask!=w.mask||rgba.size()!=w.indices.size()*4)return fail("crop, indices, mask, or image extent differs");
  for(size_t i=0;i<w.indices.size();++i){for(unsigned c=0;c<3;++c)if(rgba[i*4+c]!=expected.palette[w.indices[i]][c])return fail("RGBA palette color differs");if(rgba[i*4+3]!=w.mask[i])return fail("RGBA alpha differs");}
  if(frame.at("indices_sha256").get<std::string>()!=sha256(std::as_bytes(std::span(expected.indices))))return fail("indexed frame hash differs");return {};
 }catch(const std::exception&){return fail("missing or malformed frame metadata");}
}
}
