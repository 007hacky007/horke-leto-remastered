#pragma once
#include "render/pack.h"
namespace hl {
Result<SourceCatalog> source_catalog(const std::filesystem::path& data);
Result<nlohmann::json> verify_pack(const std::filesystem::path& pack,const std::filesystem::path& data);
}
int pack_command(int argc,char** argv);
