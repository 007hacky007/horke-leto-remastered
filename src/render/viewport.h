#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
namespace hl {
struct Point {double x{},y{};};
struct LogicalPoint {int x{},y{};};
struct WindowMetrics {int width{},height{},pixel_width{},pixel_height{};};
struct FittedViewport {double x{},y{},width{},height{};bool fractional{};};
struct Viewport {
 static FittedViewport fit(int width,int height){if(width<=0||height<=0)return {};double scale=std::min(width/1920.0,height/1440.0);bool fractional=scale<1;if(!fractional)scale=std::floor(scale);return {(width-1920*scale)/2,(height-1440*scale)/2,1920*scale,1440*scale,fractional};}
 static std::optional<LogicalPoint> window_to_logical(Point p,WindowMetrics m) {
  if(m.width<=0||m.height<=0||m.pixel_width<=0||m.pixel_height<=0)return {};
  auto v=fit(m.pixel_width,m.pixel_height);
  double x=(p.x*m.pixel_width/m.width-v.x)*640/v.width;
  double y=(p.y*m.pixel_height/m.height-v.y)*480/v.height;
  if(x<0||y<0||x>=640||y>=480)return {};
  return LogicalPoint{int(x),int(y)};
 }
};
}
