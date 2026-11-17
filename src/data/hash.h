#pragma once
#include "data/result.h"
#include <filesystem>
#include <string>
namespace hl {
std::string sha256(ByteView bytes);
Result<std::string> sha256_file(const std::filesystem::path& path);
// Incremental hashing for streams too large to hold in memory.
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void update(const void* data, size_t size);
    std::string hex();
private:
    struct State;
    State* state_;
};
}
