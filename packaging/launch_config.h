#pragma once
#include "import.h"
namespace hl::delivery {
struct Paths {std::filesystem::path data,packs;};
std::string utf8(const std::filesystem::path&);
Result<ImportRequest> import_source(const std::filesystem::path&);
Result<Paths> load_paths(const std::filesystem::path&);
Result<void> save_paths(const std::filesystem::path&,const Paths&);
Result<void> validate_pack_files(const std::filesystem::path&,bool hashes);
Result<void> validate_paths(const Paths&,bool hashes=false);
Result<ImportManifest> stage_import(ImportRequest,const std::filesystem::path&);
Result<void> complete_import_manifest(const std::filesystem::path&);
Result<Paths> install_data(ImportRequest,const std::filesystem::path& profile,const std::filesystem::path& packs);
}
