#include <doctest/doctest.h>
#include "render/pack.h"
#include "image_io.h"
#include "data/hash.h"
#include "data/font.h"
#include <fstream>
using namespace hl;
namespace {
struct Fixture {
 std::filesystem::path root="work/m5/pack-fixture";nlohmann::json manifest;
 SourceCatalog catalog{{"TEST.FLC",{"source",1}}};
 Fixture(){std::filesystem::remove_all(root);std::filesystem::create_directories(root/"flc/TEST.FLC");std::vector<uint8_t> p(7*7*4,255);REQUIRE(write_png(root/"flc/TEST.FLC/overlay.png",7,7,4,p).has_value());
 manifest={{"schema_version",1},{"resource","TEST.FLC"},{"source_sha256","source"},{"scale",3},{"logical_canvas",{640,480}},{"physical_canvas",{1920,1440}},{"frame_count",1},{"atlas_gutter",2},{"atlases",nlohmann::json::array({{{"path","overlay.png"},{"size",{7,7}},{"sha256",sha256_file(root/"flc/TEST.FLC/overlay.png").value()}}})},{"frames",nlohmann::json::array({{{"frame",0},{"resource","TEST.FLC"},{"effective_delay",{{"numerator",3},{"denominator",1},{"unit","engine_updates"}}},{"palette_sha256","palette"},{"indices_sha256","mask"},{"usages",{{"overlay",{{"atlas","overlay.png"},{"rect",{2,2,3,3}},{"logical_offset",{10,20}},{"empty",false}}}}}}})}};save();}
 void save(){std::ofstream(root/"flc/TEST.FLC/index.json")<<manifest;}
};
}
TEST_SUITE("render.pack") {
TEST_CASE("frame identity offsets timing and alpha survive atlas resolution") {Fixture f;auto p=Pack::open(f.root,f.catalog);REQUIRE(p.has_value());auto a=p.value().resolve({"test.flc",0,"overlay","palette"},GraphicsMode::Original);REQUIRE(a.has_value());CHECK(a.value().x==30);CHECK(a.value().y==60);CHECK(a.value().width==3);CHECK(a.value().delay.numerator==3);auto png=p.value().load_atlas(a.value());REQUIRE(png.has_value());CHECK(png.value().pixels[3]==255);CHECK_FALSE(p.value().resolve({"TEST.FLC",1},GraphicsMode::Original).has_value());CHECK_FALSE(p.value().resolve({"TEST.FLC",0,"overlay","wrong"},GraphicsMode::Original).has_value());}
TEST_CASE("missing remastered frames fall back while keeping mode-specific cache identity") {Fixture f;auto p=Pack::open(f.root,f.catalog);auto empty=Pack::open(f.root,{});REQUIRE(p.has_value());REQUIRE(empty.has_value());FrameResolver resolver(p.value(),&empty.value());auto original=resolver.resolve({"TEST.FLC",0},GraphicsMode::Original);auto fallback=resolver.resolve({"TEST.FLC",0},GraphicsMode::Remastered);REQUIRE(original.has_value());REQUIRE(fallback.has_value());CHECK(original.value().atlas==fallback.value().atlas);CHECK(original.value().identity!=fallback.value().identity);CHECK(fallback.value().mode==GraphicsMode::Remastered);CHECK_FALSE(resolver.resolve({"MISSING.FLC",0},GraphicsMode::Remastered).has_value());}
TEST_CASE("bad dimensions count source identity and atlas edges fail before upload") {
 for(int variant=0;variant<8;++variant){Fixture f;if(variant==0)f.manifest["source_sha256"]="other";if(variant==1)f.manifest["frame_count"]=2;if(variant==2)f.manifest["frames"][0]["usages"]["overlay"]["rect"]={0,0,3,3};if(variant==3)f.manifest["frames"][0]["resource"]="OTHER.FLC";if(variant==4)f.manifest["frames"][0]["usages"]["overlay"]["rect"]={2,2,4,3};if(variant==5)f.manifest["scale"]=2;if(variant==6)f.manifest["frames"][0]["usages"]["overlay"]["pixel_sha256"]=123;if(variant==7){f.manifest["atlases"][0]["path"]="pages/overlay.png";f.manifest["frames"][0]["usages"]["overlay"]["atlas"]="pages/overlay.png";}f.save();CHECK_FALSE(Pack::open(f.root,f.catalog).has_value());}
}
TEST_CASE("absolute parent drive backslash and symlink atlas paths are rejected") {
 for(auto path:{"/tmp/a.png","../a.png","C:/a.png","..\\a.png","link.png"}){Fixture f;if(std::string(path)=="link.png")std::filesystem::create_symlink("overlay.png",f.root/"flc/TEST.FLC/link.png");f.manifest["atlases"][0]["path"]=path;f.save();CHECK_FALSE(Pack::open(f.root,f.catalog).has_value());}
}
TEST_CASE("empty frames remain empty and corrupted atlases fail validation") {Fixture f;f.manifest["frames"][0]["usages"]["overlay"]["empty"]=true;f.manifest["frames"][0]["usages"]["overlay"]["rect"]={2,2,1,1};f.save();auto p=Pack::open(f.root,f.catalog);REQUIRE(p.has_value());auto a=p.value().resolve({"TEST.FLC",0},GraphicsMode::Original);REQUIRE(a.has_value());CHECK(a.value().empty);std::ofstream(f.root/"flc/TEST.FLC/overlay.png")<<"bad";CHECK_FALSE(p.value().load_atlas(a.value()).has_value());}
}

TEST_SUITE("render.pack") {
TEST_CASE("font atlas verifies source mapping metrics and pixels for both modes") {
 Fixture f;std::filesystem::create_directories(f.root/"font");
 std::vector<uint8_t> pixels(448*832*4,255);REQUIRE(write_png(f.root/"font/glyphs.png",448,832,4,pixels).has_value());
 auto glyphs=nlohmann::json::array();std::vector<uint8_t> tile(24*48*4,255);
 std::array<std::byte,4096> raw{};auto mapping=load_original_font({raw,0}).value();
 for(int i=0;i<256;++i)glyphs.push_back({{"index",i},{"codepoint",uint32_t(mapping.glyphs[i].codepoint)},{"rect",{(i%16)*28+2,(i/16)*52+2,24,48}},{"advance",24},{"baseline",36},{"output_pixel_sha256",sha256(std::as_bytes(std::span(tile)))}});
 nlohmann::json font{{"schema_version",1},{"source_sha256","font-source"},{"glyph_count",256},{"logical_advance",8},{"logical_baseline",12},{"atlas",{{"file","ignored/prefix/glyphs.png"},{"sha256",sha256_file(f.root/"font/glyphs.png").value()}}},{"glyphs",glyphs}};
 auto save=[&]{std::ofstream(f.root/"font/index.json")<<font;};save();
 auto pack=Pack::open(f.root,f.catalog,"font-source");REQUIRE(pack.has_value());FrameResolver resolver(pack.value());
 for(auto mode:{GraphicsMode::Original,GraphicsMode::Remastered}){auto a=resolver.glyph(130,mode);REQUIRE(a.has_value());CHECK(a.value().width==24);CHECK(a.value().height==48);CHECK(a.value().u==58);CHECK(a.value().v==418);CHECK(a.value().mode==mode);CHECK(pack.value().load_atlas(a.value()).has_value());}
 CHECK_FALSE(Pack::open(f.root,f.catalog,"wrong-source").has_value());
 for(int variant=0;variant<5;++variant){auto original=font;if(variant==0)font["glyphs"][130]["codepoint"]=0;if(variant==1)font["glyphs"][0]["advance"]=23;if(variant==2)font["glyphs"][0]["rect"]={0,0,24,48};if(variant==3)font["glyph_count"]=255;if(variant==4)font["glyphs"][0]["output_pixel_sha256"]=1;save();CHECK_FALSE(Pack::open(f.root,f.catalog,"font-source").has_value());font=original;}
 font["glyphs"][0]["output_pixel_sha256"]=std::string(64,'0');save();pack=Pack::open(f.root,f.catalog,"font-source");REQUIRE(pack.has_value());auto a=pack.value().glyph(0,GraphicsMode::Original);REQUIRE(a.has_value());CHECK_FALSE(pack.value().load_atlas(a.value()).has_value());
}
}

TEST_SUITE("render.pack") {
TEST_CASE("offline palette variants resolve exact live states and validate their associations") {
 Fixture f;std::string palette(64,'a');auto folder=f.root/"variants"/palette;std::filesystem::create_directories(folder/"flc/TEST.FLC");
 auto manifest=f.manifest;manifest["frames"][0]["palette_sha256"]=palette;
 std::filesystem::copy_file(f.root/"flc/TEST.FLC/overlay.png",folder/"flc/TEST.FLC/overlay.png");
 std::ofstream(folder/"flc/TEST.FLC/index.json")<<manifest;
 nlohmann::json index{{"schema_version",1},{"variants",nlohmann::json::array({{{"palette_sha256",palette},{"path",palette},{"resources",{"TEST.FLC"}}}})}};
 auto save=[&]{std::ofstream(f.root/"variants/index.json")<<index;};save();
 auto p=Pack::open(f.root,f.catalog);REQUIRE(p.has_value());
 for(auto mode:{GraphicsMode::Original,GraphicsMode::Remastered}){FrameResolver resolver(p.value());auto a=resolver.resolve({"TEST.FLC",0,"overlay",palette},mode);REQUIRE(a.has_value());CHECK(a.value().palette==palette);CHECK(a.value().atlas.parent_path()==folder/"flc/TEST.FLC");CHECK(p.value().load_atlas(a.value()).has_value());}
 index["variants"][0]["path"]="../escape";save();CHECK_FALSE(Pack::open(f.root,f.catalog).has_value());
 index["variants"][0]["path"]=palette;index["variants"][0]["palette_sha256"]=std::string(64,'b');save();CHECK_FALSE(Pack::open(f.root,f.catalog).has_value());
}
}

TEST_SUITE("render.pack") {
TEST_CASE("trusted CLI root spelling survives atlas load but swapped descendants fail") {
 Fixture f;auto p=Pack::open(std::filesystem::path(".")/f.root,f.catalog);REQUIRE(p.has_value());auto a=p.value().resolve({"TEST.FLC",0},GraphicsMode::Original);REQUIRE(a.has_value());CHECK(p.value().load_atlas(a.value()).has_value());
 auto file=f.root/"flc/TEST.FLC/overlay.png";std::filesystem::rename(file,file.parent_path()/"real.png");std::filesystem::create_symlink("real.png",file);CHECK_FALSE(p.value().load_atlas(a.value()).has_value());
}
}
TEST_SUITE("render.pack") {
TEST_CASE("preload includes later animation pages and owns their validation metadata") {
 Fixture f;auto folder=f.root/"flc/TEST.FLC";
 std::filesystem::copy_file(folder/"overlay.png",folder/"later.png");
 auto atlas=f.manifest["atlases"][0];atlas["path"]="later.png";f.manifest["atlases"].push_back(atlas);
 auto frame=f.manifest["frames"][0];frame["frame"]=1;frame["palette_sha256"]="later-palette";frame["usages"]["overlay"]["atlas"]="later.png";f.manifest["frames"].push_back(frame);
 f.manifest["frame_count"]=2;f.catalog["TEST.FLC"].frames=2;f.save();
 auto pack=Pack::open(f.root,f.catalog);REQUIRE(pack.has_value());
 auto pages=pack.value().pages({"TEST.FLC",0,"overlay","palette"},GraphicsMode::Remastered);REQUIRE(pages.has_value());REQUIRE(pages.value().size()==2);
 CHECK(pages.value()[1].atlas.filename()=="later.png");CHECK(pages.value()[1].mode==GraphicsMode::Remastered);
 auto source=pack.value().atlas_source(pages.value()[1]);pack=Pack::open(f.root,{});
 CHECK(source.load_atlas(pages.value()[1]).has_value());
 std::ofstream(folder/"later.png")<<"corrupt";CHECK_FALSE(source.load_atlas(pages.value()[1]).has_value());
}
}
