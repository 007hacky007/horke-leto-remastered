#pragma once
#include "data/flc.h"
namespace hl {
struct TransparencyRule {std::optional<uint8_t> index;std::string provenance;};
struct CroppedFrame {uint32_t x{},y{},width{},height{};bool empty{};std::vector<uint8_t> indices,mask;};
Result<CroppedFrame> crop_frame(const IndexedFrame&,const TransparencyRule&);
}
