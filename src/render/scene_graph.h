#pragma once
#include "data/flc.h"
#include "render/pack.h"
#include "data/character.h"
#include "data/frame_crop.h"
#include "render/text_overlay.h"
namespace hl {
Result<CroppedFrame> crop_dos_surface(const IndexedFrame&);
struct RenderCapture {int width{},height{};std::vector<uint8_t> pixels;};
Result<void> validate_capture(const RenderCapture& capture);
struct RenderSurface {int x{},y{},z{},width{},height{};std::vector<uint8_t> rgba;FrameSource source;bool opaque_base{};std::vector<GlyphPlacement> glyphs;std::array<uint8_t,3> text_color{255,255,255};bool dos_text_clip{};};
inline std::array<int,2> surface_clip(const RenderSurface& s){return {(!s.opaque_base&&int64_t(s.x)+s.width>640?639:640)*3,(!s.opaque_base&&int64_t(s.y)+s.height>480?479:480)*3};}
Result<std::vector<uint8_t>> flatten_surfaces(std::vector<RenderSurface> surfaces);
struct AssetPlacement {double x{},y{},width{},height{};};
inline AssetPlacement place_asset(const FrameAsset& a,int x,int y,double sx=1,double sy=1){return {x*3+a.x*sx,y*3+a.y*sy,a.width*sx,a.height*sy};}
struct CharacterPlacement {IndexedFrame frame;int x{},y{};};
Result<CharacterPlacement> place_character_frame(const IndexedFrame&,CharacterOffset,int x,int y,int scale);
Result<CharacterPlacement> place_character_frame(const IndexedFrame&,const CroppedFrame&,CharacterOffset,int x,int y,int scale,int origin_width=0,int origin_height=0);
struct SceneLayer {const IndexedFrame* frame{};int32_t x{},y{},z{};std::optional<uint8_t> transparent;};
struct SceneGraph {
 std::vector<SceneLayer> layers;
 Result<std::vector<uint8_t>> compose()const;
 Result<std::vector<uint8_t>> compose(int x,int y,int width,int height)const;
};
}
