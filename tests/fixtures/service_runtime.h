#pragma once
#include "fixtures/scene.h"
#include "fixtures/archive.h"
#include "fixtures/script/object_fixture.h"
#include "data/adb.h"
#include <fstream>
inline std::unique_ptr<hl::IntroRuntime> service_runtime(std::initializer_list<unsigned> code,const std::vector<std::string>& resources,const std::map<std::string,std::vector<std::byte>>& replacements={},const std::filesystem::path& dir="work/m4/service-fixture") {
 auto db=hl::AdbDatabase::open("work/import/DATA.ADB");REQUIRE(db.has_value());
 auto script=script_fixture::object(code,resources);std::filesystem::create_directories(dir);
 for(auto name:{"GFX.GRP","HL.EXE","JOKES.CHR","MAJER.CHR"})if(!std::filesystem::exists(dir/name))std::filesystem::create_symlink(std::filesystem::absolute(std::filesystem::path("work/import")/name),dir/name);
 auto count=db.value().entries().size();std::vector<std::byte> bytes(20+count*40);fixture::u32(bytes,0,666);fixture::u32(bytes,8,uint32_t(count));fixture::u32(bytes,12,uint32_t(count));fixture::u32(bytes,16,31);
 for(size_t i=0;i<count;++i){const auto& e=db.value().entries()[i];auto data=db.value().read(e.name);REQUIRE(data.has_value());hl::ByteView b=e.name=="main"?hl::ByteView(script):replacements.contains(e.name)?hl::ByteView(replacements.at(e.name)):data.value();fixture::u32(bytes,20+i*40,uint32_t(bytes.size()-20-count*40));fixture::text(bytes,24+i*40,e.name);fixture::u32(bytes,56+i*40,uint32_t(b.size()));bytes.insert(bytes.end(),b.begin(),b.end());}
 {std::ofstream out(dir/"DATA.ADB",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
 auto runtime=hl::IntroRuntime::open(dir);REQUIRE(runtime.has_value());return std::move(runtime.value());
}
