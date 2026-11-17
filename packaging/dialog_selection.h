#pragma once
#include <atomic>
#include <string>
namespace hl::delivery {
struct DialogSelection {
 std::atomic<bool> done{false};
 std::string file,error;
};
}
