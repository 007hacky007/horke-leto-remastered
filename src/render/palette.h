#pragma once
#include "data/result.h"
#include "data/flc.h"
#include <array>
namespace hl {
// gain/bias reproduce palette fades; brightness/contrast are gfx_bright/gfx_contrast divided by 50.
struct PalettePostprocess {float gain{1},bias{};float brightness{1},contrast{1};bool adjusted()const{return brightness!=1||contrast!=1;}};
Result<PalettePostprocess> palette_postprocess(unsigned step,bool flash);
constexpr uint8_t expand_dac(uint8_t component){return uint8_t((component<<2)|(component>>4));}
using Palette=std::array<std::array<uint8_t,3>,256>;
struct PaletteView {Palette source{},current{};bool visible{true};int brightness{50},contrast{50};};
Result<PalettePostprocess> palette_postprocess(const PaletteView&);
Result<std::string> frame_palette_identity(const IndexedFrame&,const Palette&,std::optional<uint8_t> transparent);
// HL.EXE 0x32f90 per pixel: in HSV, V'=((V-0.5)*contrast+0.5)*brightness clamped to 0..1; hue and saturation stay.
void adjust_picture(std::span<uint8_t> rgba,float brightness,float contrast);
uint8_t dialogue_shade_source_index(const Palette& palette,uint8_t index);
uint8_t dialogue_shade_index(const Palette& palette,uint8_t index);
Result<Palette> interpolate_palette(const Palette& from,const Palette& to,unsigned step);
}
