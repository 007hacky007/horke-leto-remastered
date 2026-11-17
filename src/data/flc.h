#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
namespace hl {
enum class DelayUnit {seconds,engine_updates};
struct FrameDelay {uint64_t numerator{};uint64_t denominator{1000};DelayUnit unit{DelayUnit::seconds};};
struct FrameSource {std::string resource;uint32_t frame{};int x{},y{},width{},height{};std::string palette;};
struct IndexedFrame {std::vector<uint8_t> indices;std::array<std::array<uint8_t,3>,256> palette{};uint16_t logical_width{},logical_height{};uint32_t frame_index{};FrameDelay declared_delay,effective_delay;FrameSource source;};
class FrameClock {
 uint64_t numerator_{},denominator_{1};
public:
 Result<void> advance(FrameDelay delay);
 uint64_t elapsed_nanoseconds()const;
};
class FlcDecoder {
 struct Record {size_t offset{},size{};uint16_t chunks{},delay{};};
 std::vector<std::byte> bytes_;std::vector<Record> frames_;std::string resource_;uint16_t count_{},width_{},height_{},depth_{};uint32_t delay_{};size_t next_{};IndexedFrame state_;
 Result<void> decode(const Record& frame);
public:
 static Result<FlcDecoder> open(ByteView bytes,std::string resource={});
 Result<std::optional<IndexedFrame>> next();
 Result<void> rewind();
 Result<IndexedFrame> seek(uint32_t frame_index);
 uint16_t width()const{return width_;}uint16_t height()const{return height_;}
 const std::string& resource()const{return resource_;}
 ByteView source_bytes()const{return bytes_;}
 uint16_t declared_frames()const{return count_;}size_t ring_frames()const{return frames_.size()-count_;}
};
}
