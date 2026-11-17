#pragma once
#include "data/result.h"
#include "data/flc.h"
#include <array>
namespace hl {
struct PalettePostprocess {float gain{1},bias{};};
Result<PalettePostprocess> palette_postprocess(unsigned step,bool flash);
constexpr uint8_t expand_dac(uint8_t component){return uint8_t((component<<2)|(component>>4));}
using Palette=std::array<std::array<uint8_t,3>,256>;
struct PaletteView {Palette source{},current{};bool visible{true};};
Result<PalettePostprocess> palette_postprocess(const PaletteView&);
Result<std::string> frame_palette_identity(const IndexedFrame&,const Palette&,std::optional<uint8_t> transparent);
uint8_t dialogue_shade_source_index(const Palette& palette,uint8_t index);
uint8_t dialogue_shade_index(const Palette& palette,uint8_t index);
Result<Palette> interpolate_palette(const Palette& from,const Palette& to,unsigned step);
}
