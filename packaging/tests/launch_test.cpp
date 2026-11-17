#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "launch_config.h"
#include "data/hash.h"
#include "dialog_selection.h"
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
TEST_CASE("no source and malformed CUE are rejected") {
    auto root=fs::temp_directory_path()/"hl-launch-test";
    fs::remove_all(root);fs::create_directories(root);
    CHECK_FALSE(hl::delivery::import_source(root/"missing").has_value());
    std::ofstream(root/"bad.cue")<<"not a cue";
    auto source=hl::delivery::import_source(root/"bad.cue");
    REQUIRE(source.has_value());
    CHECK_FALSE(hl::import_game(source.value(),root/"data").has_value());
    fs::remove_all(root);
}
TEST_CASE("BIN finds its CUE and folder accepts nested HL") {
    auto root=fs::temp_directory_path()/"hl-launch-unicode-ž";
    fs::remove_all(root);fs::create_directories(root/"HL");
    std::ofstream(root/"disc.BIN")<<"bin";
    std::ofstream(root/"disc.CUE")<<"cue";
    auto bin=hl::delivery::import_source(root/"disc.BIN");
    REQUIRE(bin.has_value());CHECK(bin.value().cue==root/"disc.CUE");
    auto folder=hl::delivery::import_source(root);
    REQUIRE(folder.has_value());CHECK(folder.value().directory==root/"HL");
    fs::remove(root/"disc.CUE");
    CHECK_FALSE(hl::delivery::import_source(root/"disc.BIN").has_value());
    fs::remove_all(root);
}
TEST_CASE("launch settings round trip and failed write retains previous config") {
    auto root=fs::temp_directory_path()/"hl-launch-settings";
    fs::remove_all(root);fs::create_directories(root);
    hl::delivery::Paths paths{root/"data-ž",root/"packs"};
    REQUIRE(hl::delivery::save_paths(root/"launch.json",paths).has_value());
    auto loaded=hl::delivery::load_paths(root/"launch.json");
    REQUIRE(loaded.has_value());CHECK(loaded.value().data==paths.data);CHECK(loaded.value().packs==paths.packs);
    fs::create_directory(root/"launch.json.tmp");
    CHECK_FALSE(hl::delivery::save_paths(root/"launch.json",{root/"new",root/"other"}).has_value());
    CHECK(hl::delivery::load_paths(root/"launch.json").value().data==paths.data);
    fs::remove_all(root);
}
TEST_CASE("missing audio and packs cannot become a complete import") {
    auto root=fs::temp_directory_path()/"hl-launch-incomplete";
    fs::remove_all(root);fs::create_directories(root);
    CHECK_FALSE(hl::delivery::validate_paths({root,root/"packs"}).has_value());
    fs::remove_all(root);
}
TEST_CASE("pack validation rejects missing and corrupt atlas payloads") {
    auto root=fs::temp_directory_path()/"hl-launch-atlas";
    fs::remove_all(root);fs::create_directories(root/"flc/TEST.FLC");
    auto index=root/"flc/TEST.FLC/index.json";
    std::ofstream(index)<<R"({"atlases":[{"path":"atlas.png","size":[8,8],"sha256":"missing"}]})";
    CHECK_FALSE(hl::delivery::validate_pack_files(root,false).has_value());
    std::ofstream(root/"flc/TEST.FLC/atlas.png")<<"not a PNG";
    CHECK_FALSE(hl::delivery::validate_pack_files(root,false).has_value());
    fs::remove_all(root);
}
TEST_CASE("cancelled and incomplete import leave selection and data untouched") {
    auto root=fs::temp_directory_path()/"hl-launch-retain";
    fs::remove_all(root);fs::create_directories(root/"source");
    for(auto n:{"DATA.ADB","GFX.GRP","HL.EXE","JOKES.CHR","MAJER.CHR"})std::ofstream(root/"source"/n)<<"fixture";
    hl::delivery::Paths previous{root/"previous",root/"packs"};
    REQUIRE(hl::delivery::save_paths(root/"profile/launch.json",previous).has_value());
    hl::ImportRequest req;req.directory=root/"source";req.cancelled=[] {return true;};
    CHECK_FALSE(hl::delivery::install_data(req,root/"profile",previous.packs).has_value());
    req.cancelled={};
    CHECK_FALSE(hl::delivery::install_data(req,root/"profile",previous.packs).has_value());
    CHECK(hl::delivery::load_paths(root/"profile/launch.json").value().data==previous.data);
    CHECK(std::distance(fs::directory_iterator(root/"profile"),fs::directory_iterator{})==1);
    fs::remove_all(root);
}
TEST_CASE("worker file chooser publishes its selected Unicode path") {
    hl::delivery::DialogSelection selection;
    std::thread worker([&]{selection.file="hra-ž/HLFINAL2.CUE";selection.done.store(true,std::memory_order_release);});
    while(!selection.done.load(std::memory_order_acquire))std::this_thread::yield();
    CHECK(selection.file=="hra-ž/HLFINAL2.CUE");CHECK(selection.error.empty());worker.join();
}
TEST_CASE("explicit tracks replace partial embedded audio and metadata stays truthful") {
    auto root=fs::temp_directory_path()/"hl-launch-partial-audio";
    fs::remove_all(root);fs::create_directories(root/"source/audio");
    for(auto n:{"DATA.ADB","GFX.GRP","HL.EXE","JOKES.CHR","MAJER.CHR"})std::ofstream(root/"source"/n)<<"fixture";
    REQUIRE(hl::encode_flac(std::vector<int16_t>{0,1,-1,0},44100,2,root/"music.flac").has_value());
    std::ofstream(root/"source/audio/track2.flac")<<"partial old audio";
    hl::ImportRequest req;req.directory=root/"source";req.cd_tracks={root/"music.flac",root/"music.flac"};
    auto imported=hl::delivery::stage_import(req,root/"out");
    REQUIRE(imported.has_value());
    CHECK(hl::sha256_file(root/"out/audio/track2.flac").value()==hl::sha256_file(root/"music.flac").value());
    REQUIRE(hl::delivery::complete_import_manifest(root/"out").has_value());
    std::ifstream audio(root/"out/audio/manifest.json");auto j=nlohmann::json::parse(audio);
    CHECK(j.at("audio_complete")==true);CHECK(j.at("tracks").size()==2);CHECK(j.at("tracks")[0].at("sample_frames")==2);
    // Reimporting a previous import must not record a stale hash for its generated manifest.
    hl::ImportRequest repeat;repeat.directory=root/"out";
    REQUIRE(hl::delivery::stage_import(repeat,root/"again").has_value());
    REQUIRE(hl::delivery::complete_import_manifest(root/"again").has_value());
    std::ifstream manifest(root/"again/import-manifest.json");auto m=nlohmann::json::parse(manifest);
    CHECK(m.at("audio_complete")==true);
    for(const auto& file:m.at("files")){
        CHECK(file.at("name")!="import-manifest.json");
        CHECK(file.at("destination_sha256")==hl::sha256_file(root/"again"/file.at("name").get<std::string>()).value());
    }
    fs::remove_all(root);
}
