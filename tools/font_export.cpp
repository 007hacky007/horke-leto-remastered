#include "assets.h"
#include "image_io.h"
#include "data/font.h"
#include "data/grp.h"
#include "data/hash.h"
#include <fstream>
namespace hl {
Result<nlohmann::json> export_font(const std::filesystem::path& data,const std::filesystem::path& output){
 auto b=read_file(data/"HL.EXE");if(!b.has_value())return b.error();auto f=load_original_font({b.value(),252284});if(!f.has_value())return f.error();std::filesystem::create_directories(output);
 nlohmann::json j{{"schema_version",1},{"source","HL.EXE"},{"source_sha256",sha256(b.value())},{"source_byte_offset",252284},{"source_virtual_address","0x44b7c"},{"font_sha256",sha256(ByteView(b.value()).subspan(252284,4096))},{"glyph_count",256},{"width",8},{"height",16},{"advance",8},{"baseline",12},{"baseline_provenance","Body glyphs end before row 12. DOS draws from a top-left anchor; it stores no separate baseline field."},{"glyphs",nlohmann::json::array()}};
 std::vector<uint8_t> atlas(128*256);for(unsigned i=0;i<256;++i){auto& g=f.value().glyphs[i];j["glyphs"].push_back({{"index",i},{"codepoint",uint32_t(g.codepoint)},{"rows",g.rows}});for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x)atlas[(i/16*16+y)*128+i%16*8+x]=(g.rows[y]&(128>>x))?255:0;}
 auto written=write_png(output/"glyphs.png",128,256,1,atlas);if(!written.has_value())return written.error();std::array<uint8_t,11> text{'K',' ','t',0xa0,'b','o',0xa9,'i',0xa8,'t','i'};auto rendered=render_font(f.value(),text);if(!rendered.has_value())return rendered.error();written=write_png(output/"contact.png",88,16,1,rendered.value());if(!written.has_value())return written.error();
 std::ofstream raw(output/"original-font.bin",std::ios::binary);raw.write(reinterpret_cast<const char*>(b.value().data()+252284),4096);raw.close();std::ofstream out(output/"glyphs.json");out<<j.dump(2)<<'\n';if(!raw||!out)return DataError{"","font",0,"font export write failed"};return j;
}
}
