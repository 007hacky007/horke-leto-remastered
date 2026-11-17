#include "render/scene_graph.h"
#include "data/frame_crop.h"
#include <algorithm>
namespace hl {
Result<CroppedFrame> crop_dos_surface(const IndexedFrame& f) {
 auto result=crop_frame(f,{0,"DOS surface bounds 0x108f3"});if(!result.has_value()||result.value().empty)return result;
 auto& crop=result.value();size_t last=f.indices.size();while(last&&f.indices[last-1]==0)--last;--last;
 // DOS 0x109ba stops before a column address equal to the final pixel.
 unsigned left=crop.x;bool found=false;
 for(unsigned block=0;block<f.logical_width&&!found;block+=4){std::array<uint8_t,4> column{};
  for(unsigned y=crop.y;y<crop.y+crop.height;++y){for(unsigned k=0;k<4&&block+k<f.logical_width;++k)column[k]|=f.indices[size_t(y)*f.logical_width+block+k];
   if(size_t(y+1)*f.logical_width+block>=last)break;
  }
  for(unsigned k=0;k<4&&block+k<f.logical_width;++k)if(column[k]){left=block+k;found=true;break;}
 }
 if(left>crop.x&&left<crop.x+crop.width){unsigned skip=left-crop.x,width=crop.width-skip;std::vector<uint8_t> indices,mask;
  for(unsigned y=0;y<crop.height;++y)for(unsigned x=skip;x<crop.width;++x){indices.push_back(crop.indices[size_t(y)*crop.width+x]);mask.push_back(crop.mask[size_t(y)*crop.width+x]);}
  crop.x=left;crop.width=width;crop.indices=std::move(indices);crop.mask=std::move(mask);
 }
 return result;
}
Result<void> validate_capture(const RenderCapture& capture){if(capture.width<=0||capture.height<=0||uint64_t(capture.width)*capture.height*4!=capture.pixels.size())return DataError{"","renderer capture",0,"capture dimensions do not match RGBA storage"};return {};}
Result<std::vector<uint8_t>> flatten_surfaces(std::vector<RenderSurface> surfaces) {
 std::vector<uint8_t> out(640*480*4);for(size_t i=3;i<out.size();i+=4)out[i]=255;
 std::stable_sort(surfaces.begin(),surfaces.end(),[](const auto& a,const auto& b){return a.z>b.z;});
 for(const auto& surface:surfaces) {
  if(surface.width<=0||surface.height<=0||uint64_t(surface.width)*surface.height*4!=surface.rgba.size())return DataError{"","renderer",0,"invalid render surface"};
  for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x) {
   int64_t cx=int64_t(x)+surface.x,cy=int64_t(y)+surface.y;if(cx<0||cx>=640||cy<0||cy>=480)continue;size_t source=(size_t(y)*surface.width+x)*4,target=(size_t(cy)*640+size_t(cx))*4;
   unsigned alpha=surface.rgba[source+3];for(size_t c=0;c<3;++c)out[target+c]=uint8_t((unsigned(surface.rgba[source+c])*alpha+unsigned(out[target+c])*(255-alpha))/255);
  }
 }return out;
}
Result<CharacterPlacement> place_character_frame(const IndexedFrame& f,CharacterOffset offset,int x,int y,int scale) {
 auto crop=crop_dos_surface(f);if(!crop.has_value())return crop.error();return place_character_frame(f,crop.value(),offset,x,y,scale);
}
Result<CharacterPlacement> place_character_frame(const IndexedFrame& f,const CroppedFrame& c,CharacterOffset offset,int x,int y,int scale,int origin_width,int origin_height) {
 if(scale<=0||scale>10000)return DataError{"","character",0,"invalid character projection"};
 if(uint64_t(c.width)*c.height!=c.indices.size())return DataError{"","character",0,"invalid character crop"};
 int w=int(c.width)*scale/10000,h=int(c.height)*scale/10000;CharacterPlacement out;
 out.x=x-(origin_width?origin_width:int(c.width))*scale/20000+offset.x*scale/10000;out.y=y-(origin_height?origin_height:int(c.height))*scale/10000+offset.y*scale/10000;
 if(!w||!h)return DataError{"","character",0,"empty projected character surface"};
 out.frame.palette=f.palette;
 out.frame.source={f.source.resource,f.source.frame,int(c.x),int(c.y),int(c.width),int(c.height)};
 out.frame.logical_width=uint16_t(w);out.frame.logical_height=uint16_t(h);out.frame.indices.resize(size_t(w)*h);
 // DOS samples with the original scale, before the output extent is rounded.
 for(int py=0;py<h;++py)for(int px=0;px<w;++px)out.frame.indices[size_t(py)*w+px]=c.indices[size_t(py*10000/scale)*c.width+size_t(px*10000/scale)];return out;
}
Result<std::vector<uint8_t>> SceneGraph::compose()const {return compose(0,0,640,480);}
Result<std::vector<uint8_t>> SceneGraph::compose(int x,int y,int width,int height)const {
 if(width<=0||height<=0||width>640||height>480)return DataError{"","scene",0,"invalid composition extent"};
 std::vector<uint8_t> canvas(size_t(width)*height);auto sorted=layers;
 // DOS surfaces use descending Z, with background at 480 and characters at 480-footY.
 std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.z>b.z;});
 for(const auto& layer:sorted) {
  if(!layer.frame)return DataError{"","scene",0,"null layer frame"};const auto& f=*layer.frame;
  if(!f.logical_width||!f.logical_height||size_t(f.logical_width)*f.logical_height!=f.indices.size())return DataError{"","scene",0,"invalid layer dimensions"};
  int64_t left=std::max({int64_t(x),int64_t(layer.x),int64_t(0)}),top=std::max({int64_t(y),int64_t(layer.y),int64_t(0)});
  int64_t right=std::min({int64_t(x)+width,int64_t(layer.x)+f.logical_width,int64_t(640)}),bottom=std::min({int64_t(y)+height,int64_t(layer.y)+f.logical_height,int64_t(480)});
  if(layer.transparent){if(int64_t(layer.x)+f.logical_width>640)right=std::min(right,int64_t(639));if(int64_t(layer.y)+f.logical_height>480)bottom=std::min(bottom,int64_t(479));}
  for(auto cy=top;cy<bottom;++cy)for(auto cx=left;cx<right;++cx){auto v=f.indices[size_t(cy-layer.y)*f.logical_width+size_t(cx-layer.x)];if(!layer.transparent||v!=*layer.transparent)canvas[size_t(cy-y)*width+size_t(cx-x)]=v;}
 }return canvas;
}
}
