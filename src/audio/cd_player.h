#pragma once
#include "data/result.h"
#include <filesystem>
#include <memory>
#include <optional>
namespace hl {
class CdPlayer {
 struct State;std::unique_ptr<State> state_;
 std::unique_ptr<CdPlayer> device_;std::optional<DataError> device_error_;
 Result<void> seek_local(uint64_t sample);
public:
 CdPlayer();~CdPlayer();
 CdPlayer(const CdPlayer&)=delete;CdPlayer& operator=(const CdPlayer&)=delete;
 Result<void> play(unsigned track,const std::filesystem::path& file,bool loop);
 Result<void> seek(uint64_t sample);
 Result<void> mix(std::span<float> output,uint32_t output_rate);
 Result<void> mix_device(std::span<float> output,uint32_t output_rate);
 void stop();Result<void> resume();
 unsigned track()const;uint64_t position()const;bool playing()const;
 size_t buffer_capacity_frames()const{return 4096;}
};
}
