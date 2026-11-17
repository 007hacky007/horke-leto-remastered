#pragma once
#include "data/flc.h"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
namespace hl {
enum class GraphicsMode {Original,Remastered};
struct ResourceFrameKey {std::string resource;uint32_t frame{};std::string usage="overlay",palette;};
struct SourceResource {std::string sha256;uint32_t frames{};};
using SourceCatalog=std::map<std::string,SourceResource>;
Result<SourceCatalog> source_catalog(const std::filesystem::path& data);
struct AtlasImage {int width{},height{};std::vector<uint8_t> pixels;};
class Pack;
struct FrameAsset {
 const Pack* owner{};
 std::filesystem::path atlas;std::string atlas_hash,pixel_hash,mask_hash,palette,identity;
 int atlas_width{},atlas_height{},u{},v{},width{},height{},x{},y{};bool empty{};
 FrameDelay delay;GraphicsMode mode{GraphicsMode::Original};
};
class Pack {
 std::map<std::string,nlohmann::json> manifests_;std::map<std::string,std::filesystem::path> folders_;
 std::map<std::string,std::map<std::string,std::filesystem::path>> atlas_paths_;std::filesystem::path root_;
 static Result<Pack> open_base(const std::filesystem::path&,const SourceCatalog&,const std::string&);
 std::array<FrameAsset,256> glyphs_;bool has_font_{};
public:
 static Result<Pack> open(const std::filesystem::path&,const SourceCatalog&,const std::string& font_sha256={});
 Result<FrameAsset> glyph(uint8_t,GraphicsMode)const;
 Result<FrameAsset> resolve(const ResourceFrameKey&,GraphicsMode)const;
 Result<AtlasImage> load_atlas(const FrameAsset&)const;
 Result<std::vector<FrameAsset>> pages(const ResourceFrameKey&,GraphicsMode)const;
 Pack atlas_source(const FrameAsset&)const;
 size_t resources()const{return manifests_.size();}
};
class FrameResolver {
 const Pack& original_;const Pack* remastered_{};
public:
 FrameResolver(const Pack& original,const Pack* remastered=nullptr):original_(original),remastered_(remastered){}
 Result<FrameAsset> glyph(uint8_t index,GraphicsMode mode)const {return original_.glyph(index,mode);}
 Result<FrameAsset> resolve(const ResourceFrameKey& key,GraphicsMode mode)const {if(mode==GraphicsMode::Remastered&&remastered_){auto replacement=remastered_->resolve(key,mode);if(replacement.has_value())return replacement;}return original_.resolve(key,mode);}
};
Result<std::filesystem::path> safe_pack_path(const std::filesystem::path& root,const std::string& relative);
std::string resource_key(std::string);
}
