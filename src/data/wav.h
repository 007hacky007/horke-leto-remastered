#pragma once
#include "data/result.h"
#include <cstdint>
#include <vector>
namespace hl {
struct PcmAudio {uint32_t rate{};uint16_t channels{};uint64_t frames{};std::vector<int16_t> samples;std::string sample_format{"signed16"},channel_order;};
Result<PcmAudio> decode_wav(ByteView bytes);
}
