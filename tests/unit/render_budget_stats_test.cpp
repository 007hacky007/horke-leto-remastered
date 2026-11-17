#include <doctest/doctest.h>
#include "benchmark.h"
#include <limits>
TEST_SUITE("render.budget") {
TEST_CASE("Mac budget requires paired CPU plus GPU p95 below eight milliseconds") {
 auto s=hl::timing_summary({1,2,3,10});CHECK(s.at("max_ms")==10);CHECK(s.at("mean_ms")==4);CHECK(s.at("p95_ms")==10);
 CHECK(hl::accelerated_budget_passes({4.3,1},{3.6,6.9}));
 CHECK_FALSE(hl::accelerated_budget_passes({4.3,1},{3.7,1}));
 CHECK_FALSE(hl::accelerated_budget_passes({1,1},{1,8.607334}));
 CHECK_FALSE(hl::accelerated_budget_passes({},{}));
 CHECK_FALSE(hl::accelerated_budget_passes({1,2},{1}));
 CHECK_FALSE(hl::accelerated_budget_passes({-1},{1}));
 CHECK_FALSE(hl::accelerated_budget_passes({1},{std::numeric_limits<double>::quiet_NaN()}));
 std::vector<double> cpu(100,3),gpu(100,4);gpu[0]=20;
 CHECK(hl::accelerated_budget_passes(cpu,gpu));
 for(size_t i=0;i<6;++i)gpu[i]=5;
 CHECK_FALSE(hl::accelerated_budget_passes(cpu,gpu));
 cpu[0]=-1;gpu.assign(100,1);
 CHECK_FALSE(hl::accelerated_budget_passes(cpu,gpu));
}
TEST_CASE("Windows budget retains the original 35 Hz frame period") {
 CHECK(hl::original_rate_budget_passes({16,28.5}));
 CHECK_FALSE(hl::original_rate_budget_passes({16,28.6}));
 CHECK_FALSE(hl::original_rate_budget_passes({}));
}
}
