#pragma once
#include <vector>
#include <string>
namespace hl {struct GpuTimingInterval {int frame;double begin,end;};std::vector<GpuTimingInterval> gpu_timing_intervals();bool install_gpu_timing();std::string gpu_timing_device();void gpu_timing_frame(int index);std::vector<double> gpu_timings(unsigned frames);void remove_gpu_timing();}
