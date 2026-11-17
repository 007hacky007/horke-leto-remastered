// Shared helpers for the native builder: errors, hashing, JSON files, checkpoints, processes.
#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace hb {
namespace fs = std::filesystem;
using json = nlohmann::json;
using Bytes = std::vector<uint8_t>;

// A problem the player can understand; the message is Czech and shown as is.
struct UserError : std::runtime_error { using std::runtime_error::runtime_error; };

// Progress and log lines. Lines starting with "[stage] ", "[detail] " carry structure for the UI.
using Emit = std::function<void(const std::string&)>;

std::string sha_bytes(const void* data, size_t size);
inline std::string sha_bytes(const Bytes& b) { return sha_bytes(b.data(), b.size()); }
inline std::string sha_text(const std::string& s) { return sha_bytes(s.data(), s.size()); }
std::string sha_file(const fs::path& path);

Bytes read_file(const fs::path& path);
void write_file(const fs::path& path, const void* data, size_t size);
json read_json(const fs::path& path);
void write_json(const fs::path& path, const json& value);  // indent 2, ASCII, atomic

// A stage is valid when its key matches and every listed file still has its recorded hash.
class Checkpoint {
public:
    Checkpoint(fs::path path, std::string key) : path_(std::move(path)), key_(std::move(key)) {}
    bool valid() const;
    void commit(const std::vector<fs::path>& files) const;
private:
    fs::path path_; std::string key_;
};

std::vector<fs::path> files_under(const fs::path& root);
void remove_tree(const fs::path& path);

// Run a program and wait. Output goes to `log` when given, otherwise it is discarded.
int run_process(const std::vector<std::string>& argv, const fs::path& log = {});

// Path of the running executable, and the builder's private data (payload) folder.
fs::path executable_path();

// Developer filter: HB_ONLY="A.FLC,B.FLC" limits the art stages to those resources.
bool selected_resource(const std::string& name);

// Set from the UI when the player closes the window; long loops check it.
extern std::atomic<bool> cancelled;
void check_cancelled();
}
