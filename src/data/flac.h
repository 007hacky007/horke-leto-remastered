#pragma once
#include "data/wav.h"
#include <filesystem>
namespace hl {Result<PcmAudio> decode_flac(const std::filesystem::path&);}
