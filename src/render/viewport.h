#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
namespace hl {
enum class ScalingMode {Fill,Whole,Stretch};
struct Point {double x{},y{};};
struct LogicalPoint {int x{},y{};};
struct WindowMetrics {int width{},height{},pixel_width{},pixel_height{};};
struct FittedViewport {double x{},y{},width{},height{};bool fractional{};};
struct Viewport {
 static FittedViewport fit(int width,int height,ScalingMode mode=ScalingMode::Fill){
  if(width<=0||height<=0)return {};
  if(mode==ScalingMode::Stretch)return {0,0,double(width),double(height),true};
  double scale=std::min(width/1920.0,height/1440.0);
  double vw=width,vh=width*0.75;
  if(vh>height){vh=height;vw=height*4.0/3.0;}
  if(mode==ScalingMode::Whole&&scale>=1){scale=std::floor(scale);vw=1920*scale;vh=1440*scale;}
  return {(width-vw)/2,(height-vh)/2,vw,vh,scale!=std::floor(scale)};
 }
 static std::optional<LogicalPoint> window_to_logical(Point p,WindowMetrics m,ScalingMode mode=ScalingMode::Fill) {
  if(m.width<=0||m.height<=0||m.pixel_width<=0||m.pixel_height<=0)return {};
  auto v=fit(m.pixel_width,m.pixel_height,mode);
  double x=(p.x*m.pixel_width/m.width-v.x)*640/v.width;
  double y=(p.y*m.pixel_height/m.height-v.y)*480/v.height;
  if(x<0||y<0||x>=640||y>=480)return {};
  return LogicalPoint{int(x),int(y)};
 }
};
}
