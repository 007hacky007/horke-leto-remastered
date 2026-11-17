#include "gpu_timing.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <mutex>
#include <map>
namespace hl {
namespace {
std::mutex mutex;std::map<int,double> samples;std::vector<GpuTimingInterval> intervals;int frame=-1;unsigned generation=0;
Method method;IMP original;std::string device_name;
void commit(id<MTLCommandBuffer> buffer,SEL selector){
 int index=frame;unsigned session=generation;
 if(index>=0)[buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){double begin=completed.GPUStartTime,end=completed.GPUEndTime;if(begin>0&&end>=begin){std::lock_guard lock(mutex);if(session==generation){samples[index]+=(end-begin)*1000;intervals.push_back({index,begin,end});}}}];
 reinterpret_cast<void(*)(id,SEL)>(original)(buffer,selector);
}
}
bool install_gpu_timing(){
 if(original)return false;
 {std::lock_guard lock(mutex);++generation;samples.clear();intervals.clear();frame=-1;}
 @autoreleasepool {
  auto device=MTLCreateSystemDefaultDevice();device_name=device.name.UTF8String?device.name.UTF8String:"";auto queue=[device newCommandQueue];auto buffer=[queue commandBuffer];if(!buffer)return false;
  // Observe this process's Metal submissions through public completion timestamps.
  // No SDL internals, global setting, disk capture or system instrumentation is used.
  method=class_getInstanceMethod(object_getClass(buffer),@selector(commit));if(!method)return false;
  original=method_setImplementation(method,reinterpret_cast<IMP>(commit));return original!=nullptr;
 }
}
std::string gpu_timing_device(){return device_name;}
void gpu_timing_frame(int index){frame=index;}
std::vector<double> gpu_timings(unsigned frames){std::lock_guard lock(mutex);std::vector<double> out;for(unsigned i=0;i<frames;++i){auto it=samples.find(int(i));if(it==samples.end())return {};out.push_back(it->second);}return out;}
std::vector<GpuTimingInterval> gpu_timing_intervals(){std::lock_guard lock(mutex);return intervals;}
void remove_gpu_timing(){if(original)method_setImplementation(method,original);original=nullptr;frame=-1;}
}
