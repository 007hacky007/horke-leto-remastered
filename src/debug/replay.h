#pragma once
#include "debug/trace.h"
namespace hl {
class Replay {
 std::vector<InputEvent> events_; std::size_t next_{};
public:
 explicit Replay(std::vector<InputEvent> events) : events_(std::move(events)) {}
 std::span<const InputEvent> events_at(std::uint64_t tick);
};
}
