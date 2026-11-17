#include "benchmark.h"
#include "data/hash.h"
#include "gpu_timing.h"
#include "render/gpu.h"
#include "image_io.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <cmath>
#include <ctime>
namespace hl {
nlohmann::json timing_summary(std::vector<double> values){if(values.empty())return nullptr;std::sort(values.begin(),values.end());auto percentile=[&](double p){return values[size_t(std::ceil(p*values.size()))-1];};return {{"samples",values.size()},{"min_ms",values.front()},{"mean_ms",std::accumulate(values.begin(),values.end(),0.0)/values.size()},{"p50_ms",percentile(.5)},{"p95_ms",percentile(.95)},{"p99_ms",percentile(.99)},{"max_ms",values.back()}};}
bool original_rate_budget_passes(const std::vector<double>& wall){return !wall.empty()&&std::all_of(wall.begin(),wall.end(),[](double ms){return std::isfinite(ms)&&ms>=0&&ms<=1000.0/35;});}
bool accelerated_budget_passes(const std::vector<double>& cpu,const std::vector<double>& gpu){if(cpu.empty()||cpu.size()!=gpu.size())return false;for(size_t i=0;i<cpu.size();++i)if(!std::isfinite(cpu[i])||!std::isfinite(gpu[i])||cpu[i]<0||gpu[i]<0)return false;std::vector<double> combined;for(size_t i=0;i<cpu.size();++i)combined.push_back(cpu[i]+gpu[i]);return timing_summary(combined).at("p95_ms").get<double>()<8;}
}
int benchmark_command(int argc,char** argv){
 std::map<std::string,std::string> options;for(int i=1;i+1<argc;i+=2)options[argv[i]]=argv[i+1];if(options["--scene"]!="worst-case"||!options.contains("--report"))return 2;
 if(!SDL_Init(SDL_INIT_VIDEO)){std::cerr<<SDL_GetError()<<'\n';return 1;}auto window=SDL_CreateWindow("Rendering benchmark",1920,1440,SDL_WINDOW_HIDDEN);auto renderer=hl::Renderer::create(window);if(!renderer.has_value()){std::cerr<<SDL_GetError()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
 auto start=std::chrono::steady_clock::now();auto milliseconds=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
 // A full 3x background plus 32 independent alpha sprites, each 180x240 logical pixels.
 // Synthetic stress evidence complements, but does not establish, the full-game maximum.
 auto root=std::filesystem::path(options["--report"]).parent_path()/"benchmark-pack";std::filesystem::create_directories(root/"flc/STRESS.FLC");
 nlohmann::json j{{"schema_version",1},{"resource","STRESS.FLC"},{"source_sha256","synthetic"},{"scale",3},{"logical_canvas",{640,480}},{"physical_canvas",{1920,1440}},{"frame_count",33},{"atlas_gutter",2},{"atlases",nlohmann::json::array()},{"frames",nlohmann::json::array()}};
 for(int i=0;i<33;++i){int w=i?540:1920,h=i?720:1440;std::vector<uint8_t> p(size_t(w+4)*(h+4)*4);for(int y=0;y<h+4;++y)for(int x=0;x<w+4;++x){int cx=std::clamp(x-2,0,w-1),cy=std::clamp(y-2,0,h-1);size_t k=(size_t(y)*(w+4)+x)*4;p[k]=uint8_t((cx/3+i*9)%256);p[k+1]=uint8_t((cy/3+i*7)%256);p[k+2]=80;p[k+3]=i&&((cx/24+cy/24)%2)?0:255;}auto name=std::to_string(i)+".png";auto file=root/"flc/STRESS.FLC"/name;if(!hl::write_png(file,w+4,h+4,4,p).has_value())return 1;auto hash=hl::sha256_file(file);if(!hash.has_value())return 1;j["atlases"].push_back({{"path",name},{"size",{w+4,h+4}},{"sha256",hash.value()}});j["frames"].push_back({{"frame",i},{"resource","STRESS.FLC"},{"indices_sha256","synthetic"},{"palette_sha256","synthetic"},{"effective_delay",{{"numerator",1},{"denominator",1},{"unit","engine_updates"}}},{"usages",{{"overlay",{{"atlas",name},{"rect",{2,2,w,h}},{"logical_offset",{0,0}},{"empty",false}}}}}});}
 {std::ofstream out(root/"flc/STRESS.FLC/index.json");out<<j;}
 auto pack=hl::Pack::open(root,{{"STRESS.FLC",{"synthetic",33}}});if(!pack.has_value()){std::cerr<<pack.error().reason<<'\n';return 1;}
 std::vector<hl::AtlasDraw> draws;for(unsigned i=0;i<33;++i){auto a=pack.value().resolve({"STRESS.FLC",i},hl::GraphicsMode::Original);if(!a.has_value())return 1;draws.push_back({a.value(),i?int((i*37)%460):0,i?int((i*19)%240):0,i?int(i):480});}
 auto initialized=std::chrono::steady_clock::now();std::vector<double> misses,cpu,wall,present_wall,present_wait;
 auto draw=[&](){auto r=renderer.value()->present(draws,pack.value());if(!r.has_value())std::cerr<<r.error().reason<<'\n';return r.has_value()&&SDL_RenderPresent(renderer.value()->sdl());};
 auto first=std::chrono::steady_clock::now();if(!draw())return 1;auto device=SDL_GetGPURendererDevice(renderer.value()->sdl());if(device)SDL_WaitForGPUIdle(device);misses.push_back(milliseconds(std::chrono::steady_clock::now()-first));
 for(int i=0;i<60;++i)if(!draw())return 1;if(device)SDL_WaitForGPUIdle(device);
 bool timestamps=renderer.value()->backend()=="gpu/metal"&&hl::install_gpu_timing();constexpr unsigned frames=300;
 for(unsigned i=0;i<frames;++i){hl::gpu_timing_frame(int(i));auto begin=std::chrono::steady_clock::now();auto cpu_begin=std::clock();auto r=renderer.value()->present(draws,pack.value());auto present_begin=std::chrono::steady_clock::now();auto present_cpu=std::clock();if(!r.has_value()||!SDL_FlushRenderer(renderer.value()->sdl())||!SDL_RenderPresent(renderer.value()->sdl()))return 1;auto cpu_end=std::clock();auto end=std::chrono::steady_clock::now();present_wall.push_back(milliseconds(end-present_begin));present_wait.push_back(std::max(0.0,present_wall.back()-1000.0*double(cpu_end-present_cpu)/CLOCKS_PER_SEC));cpu.push_back(1000.0*double(cpu_end-cpu_begin)/CLOCKS_PER_SEC);wall.push_back(milliseconds(end-begin));}
 if(device)SDL_WaitForGPUIdle(device);auto gpu=timestamps?hl::gpu_timings(frames):std::vector<double>{};hl::remove_gpu_timing();std::vector<double> combined;for(size_t i=0;i<gpu.size();++i)combined.push_back(cpu.at(i)+gpu[i]);
 auto capture=renderer.value()->readback();auto png=std::filesystem::path(options["--report"]).parent_path()/"worst-case-readback.png";if(!capture.has_value()||!hl::write_png(png,1920,1440,4,capture.value().pixels).has_value())return 1;
 nlohmann::json report{{"schema_version",1},{"platform",SDL_GetPlatform()},{"cpu",SDL_GetNumLogicalCPUCores()},{"system_ram_mb",SDL_GetSystemRAM()},{"backend",renderer.value()->backend()},{"gpu_device",hl::gpu_timing_device()},{"gpu_creation_failure",renderer.value()->gpu_failure()},{"canvas",{1920,1440}},{"layers",33},{"scenario","synthetic full background plus 32 alpha sprites, 180x240 logical each"},{"real_scene_maximum_verified",false},{"warmup_frames",60},{"cpu_plus_gpu",hl::timing_summary(combined)},{"present_wall",hl::timing_summary(present_wall)},{"present_wait_estimate",hl::timing_summary(present_wait)},{"wait_time_source","flush/present wall minus process CPU, clamped at zero; includes blocking/pacing and scheduling, excluded from budget; not a pure vsync measurement"},{"cpu_composition",hl::timing_summary(cpu)},{"cpu_time_source","std::clock process CPU time through composition, final flush and present; excludes blocked wall time"},{"gpu",hl::timing_summary(gpu)},{"gpu_timestamp_source","Metal command-buffer GPUStartTime/GPUEndTime via process-local completion observer"},{"submission_and_present",hl::timing_summary(wall)},{"startup_ms",milliseconds(initialized-start)},{"cold_cache_ms",misses[0]},{"texture_working_set_bytes",renderer.value()->cache_bytes()},{"cache_budget_bytes",256*1024*1024},{"accelerated_limits_pass",hl::accelerated_budget_passes(cpu,gpu)},{"windows_render_period_pass",hl::original_rate_budget_passes(wall)},{"mac_combined_limit_ms",8},{"mac_budget_metric","p95 of paired process CPU plus GPU; blocked present wait excluded"},{"windows_target_hz",35},{"m5_budget_gate_pass",false},{"capture",png.filename().string()},{"capture_method","SDL_RenderReadPixels before final present"}};
 std::ofstream out(options["--report"]);out<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';renderer.value().reset();SDL_DestroyWindow(window);SDL_Quit();return out?0:1;
}
