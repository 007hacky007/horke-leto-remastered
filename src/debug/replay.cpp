#include "debug/replay.h"
namespace hl {
std::span<const InputEvent> Replay::events_at(std::uint64_t tick) {
 auto start=next_;
 while(next_ < events_.size() && events_[next_].tick <= tick) ++next_;
 return std::span<const InputEvent>(events_).subspan(start,next_-start);
}
}
