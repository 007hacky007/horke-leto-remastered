#include "render/active_areas.h"
#include <cmath>
namespace hl {
std::vector<RenderSurface> hint_surfaces(std::span<const LogicalPoint> markers,bool held){
 std::vector<RenderSurface> out;if(!held)return out;
 for(auto p:markers){RenderSurface s{p.x-3,p.y-3,-110,7,7,{}};for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){int radius=x*x+y*y;bool edge=radius>=5&&radius<=10;for(auto color:{uint8_t(255),uint8_t(235),uint8_t(160),uint8_t(edge?180:0)})s.rgba.push_back(color);}out.push_back(std::move(s));}return out;
}
}
