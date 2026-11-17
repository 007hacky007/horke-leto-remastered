#include <doctest/doctest.h>
#include "render/renderer_policy.h"
using namespace hl;
TEST_SUITE("render.renderer_policy") {
TEST_CASE("only known software D3D12 adapters are rejected") {
 CHECK(software_d3d12_adapter("direct3d12","Microsoft Basic Render Driver"));
 CHECK(software_d3d12_adapter("direct3d12","Microsoft WARP"));
 CHECK(software_d3d12_adapter("direct3d12","microsoft basic render driver"));
 CHECK_FALSE(software_d3d12_adapter("direct3d12","NVIDIA GeForce RTX 4070"));
 CHECK_FALSE(software_d3d12_adapter("direct3d12","Intel(R) UHD Graphics"));
 CHECK_FALSE(software_d3d12_adapter("metal","Microsoft Basic Render Driver"));
 CHECK_FALSE(software_d3d12_adapter("direct3d12",""));
}
TEST_CASE("startup fallback needs sustained render overruns after warmup") {
 StartupRenderBudget slow,fast,spike;
 for(int i=0;i<30;++i){CHECK_FALSE(slow.observe(100));CHECK_FALSE(fast.observe(100));CHECK_FALSE(spike.observe(100));}
 for(int i=0;i<59;++i){CHECK_FALSE(slow.observe(75));CHECK_FALSE(fast.observe(8));CHECK_FALSE(spike.observe(i==3?3290:8));}
 CHECK(slow.observe(75));CHECK_FALSE(fast.observe(8));CHECK_FALSE(spike.observe(8));
 for(int i=0;i<300;++i)CHECK_FALSE(fast.observe(80)); // startup window has closed
}
}
