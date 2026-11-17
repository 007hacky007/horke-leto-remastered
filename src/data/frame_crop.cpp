#include "data/frame_crop.h"
#include <algorithm>
namespace hl {
Result<CroppedFrame> crop_frame(const IndexedFrame& f,const TransparencyRule& rule){
 if(!f.logical_width||!f.logical_height||uint64_t(f.logical_width)*f.logical_height!=f.indices.size()||rule.provenance.empty())return DataError{"","frame",0,"invalid frame or missing transparency provenance"};
 uint32_t left=f.logical_width,top=f.logical_height,right=0,bottom=0;
 for(uint32_t y=0;y<f.logical_height;++y)for(uint32_t x=0;x<f.logical_width;++x)if(!rule.index||f.indices[size_t(y)*f.logical_width+x]!=*rule.index){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
 CroppedFrame out;if(!right){out.empty=true;return out;}out.x=left;out.y=top;out.width=right-left;out.height=bottom-top;
 for(uint32_t y=top;y<bottom;++y)for(uint32_t x=left;x<right;++x){auto v=f.indices[size_t(y)*f.logical_width+x];out.indices.push_back(v);out.mask.push_back(rule.index&&v==*rule.index?0:255);}return out;
}
}
