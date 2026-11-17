#pragma once
#include "data/result.h"
#include <filesystem>
#include <nlohmann/json.hpp>
namespace hl {Result<nlohmann::json> inventory_grp(const std::filesystem::path& data);}
namespace hl {Result<nlohmann::json> inventory_adb(const std::filesystem::path& data);}
namespace hl {Result<nlohmann::json> decode_all_flc(const std::filesystem::path& data,const std::filesystem::path& samples);}
namespace hl {Result<nlohmann::json> decode_all_wav(const std::filesystem::path&);}
int assets_command(int argc,char** argv);
#include "data/flc.h"
#include "data/frame_crop.h"
namespace hl {
Result<nlohmann::json> export_all_flc(const std::filesystem::path&,const std::filesystem::path&);
Result<std::vector<uint8_t>> restore_frame(const CroppedFrame&,uint32_t width,uint32_t height,uint8_t fill);
nlohmann::json frame_metadata(const IndexedFrame&,const TransparencyRule&);
Result<nlohmann::json> export_flc(FlcDecoder&,const TransparencyRule&,const std::filesystem::path&);
}
namespace hl {Result<nlohmann::json> export_font(const std::filesystem::path&,const std::filesystem::path&);}
namespace hl { Result<nlohmann::json> verify_scripts(const std::filesystem::path& data,const nlohmann::json& trace=nlohmann::json::array()); }
int scripts_command(int argc,char** argv);

#include "script/object.h"
namespace hl {nlohmann::json catalog_service_calls(const ScriptObject& object);}
namespace hl {nlohmann::json opcode_coverage(const ScriptObject&,const nlohmann::json& trace=nlohmann::json::array());}
