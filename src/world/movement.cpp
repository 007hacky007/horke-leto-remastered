#include "world/movement.h"
#include "script/intro_movement.h"
#include "data/byte_reader.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace hl {
int walk_direction(int dx,int dy) {double a=std::atan2(double(dy),double(dx));if(a<0)a+=6.28318;return (int((a+.39269875)/.7853975)+2)%8;}
Result<LogicalPoint> walk_target(const Region& area,int x,int y) {
 if(area.contains(x,y))return LogicalPoint{x,y};
 // DOS 0x19b92 projects onto the nearest edge, truncating toward zero.
 LogicalPoint closest{};double best=std::numeric_limits<double>::infinity();ByteReader r(area.shape);
 for(unsigned c=0;c<area.flags;++c){std::vector<LogicalPoint> contour;auto n=r.u16le().value();
  for(unsigned i=0;i<n;++i){auto px=r.u16le().value(),py=r.u16le().value();r.u16le();contour.push_back({std::bit_cast<int16_t>(px),std::bit_cast<int16_t>(py)});}
  for(size_t i=0;i<contour.size();++i){auto a=contour[i],b=contour[(i+1)%contour.size()];double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
   if(length==0)continue;double t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/length,0.0,1.0);double px=a.x+t*dx,py=a.y+t*dy,d=(px-x)*(px-x)+(py-y)*(py-y);
   if(d<best){best=d;closest={int(px),int(py)};}
  }
 }
 if(std::isfinite(best)){
  int dx=closest.x-x,dy=closest.y-y;if(dx==0&&dy==0){dx=area.x-closest.x;dy=area.y-closest.y;}
  int ax=std::abs(dx),ay=std::abs(dy),major=std::max(ax,ay),minor=std::min(ax,ay);
  // DOS 0x17ee1 tests then advances the ray, bounded to 200 probes.
  for(int step=0;step<200&&major;++step){int side=int((int64_t(step)*minor+major-minor)/major);
   LogicalPoint p{closest.x+((dx>0)-(dx<0))*(ax>=ay?step:side),closest.y+((dy>0)-(dy<0))*(ay>ax?step:side)};
   if(area.contains(p.x,p.y))return p;
  }
 }
 return DataError{"",area.label,0,"cannot project walk destination into region"};
}
void Movement::segment(){sx_=x_;sy_=y_;auto p=route_[segment_];distance_=std::max(std::abs(p.x-x_),std::abs(p.y-y_));travel_=0;fraction_=0;}
Result<Movement> Movement::begin(const Region& area,int x,int y,int tx,int ty,int speed,int near_z,int far_z) {
 auto fail=[]()->Result<Movement>{return DataError{"","movement",0,"invalid or unreachable walk destination"};};
 if(!area.contains(tx,ty)||speed<=0||speed>=10||near_z>far_z)return fail();
 if(!area.contains(x,y)) {
  // DOS1e3d7 tries eight neighbours, then a 200-probe ray toward the region anchor.
  LogicalPoint start{area.x,area.y};bool found=false;
  for(auto d:{LogicalPoint{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}})
   if(area.contains(x+d.x,y+d.y)){start={x+d.x,y+d.y};found=true;break;}
  if(!found){int dx=area.x-x,dy=area.y-y,ax=std::abs(dx),ay=std::abs(dy),major=std::max(ax,ay),minor=std::min(ax,ay);
   for(int step=0;step<200&&major;++step){int side=int((int64_t(step)*minor+major-minor)/major);
    LogicalPoint p{x+((dx>0)-(dx<0))*(ax>=ay?step:side),y+((dy>0)-(dy<0))*(ay>ax?step:side)};
    if(area.contains(p.x,p.y)){start=p;break;}
   }
  }
  x=start.x;y=start.y;
 }
 std::vector<LogicalPoint> nodes{{x,y},{tx,ty}};std::vector<std::vector<LogicalPoint>> contours;ByteReader r(area.shape);
 for(unsigned c=0;c<area.flags;++c){contours.emplace_back();auto n=r.u16le().value();for(unsigned i=0;i<n;++i){auto px=r.u16le().value(),py=r.u16le().value();r.u16le();LogicalPoint p{std::bit_cast<int16_t>(px),std::bit_cast<int16_t>(py)};nodes.push_back(p);contours.back().push_back(p);}}
 auto side=[](const auto& contour,double x,double y){bool in=false;for(size_t i=0,j=contour.size()-1;i<contour.size();j=i++){auto a=contour[j],b=contour[i];double cross=(x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);if(std::abs(cross)<.0001&&x>=std::min(a.x,b.x)&&x<=std::max(a.x,b.x)&&y>=std::min(a.y,b.y)&&y<=std::max(a.y,b.y))return 2;if((a.y>y)!=(b.y>y)&&x<double(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)in=!in;}return int(in);};
 auto visible=[&](LogicalPoint a,LogicalPoint b){int steps=std::max(std::abs(a.x-b.x),std::abs(a.y-b.y))*2;for(int i=1;i<steps;++i){double px=a.x+(b.x-a.x)*double(i)/steps,py=a.y+(b.y-a.y)*double(i)/steps;for(const auto& contour:contours){int s=side(contour,px,py);if(s!=2&&s!=side(contour,area.x,area.y))return false;}}return true;};

 // shortcut: visibility routing matches the evidenced M3 single-obstacle route; use DOS sequential contour detours before supporting other regions.
 std::vector<double> cost(nodes.size(),std::numeric_limits<double>::infinity());std::vector<size_t> previous(nodes.size(),nodes.size());std::vector<bool> done(nodes.size());cost[0]=0;
 for(size_t n=0;n<nodes.size();++n){size_t u=nodes.size();for(size_t i=0;i<nodes.size();++i)if(!done[i]&&(u==nodes.size()||cost[i]<cost[u]))u=i;if(u==nodes.size()||!std::isfinite(cost[u]))break;if(u==1)break;done[u]=true;
  for(size_t v=0;v<nodes.size();++v)if(!done[v]&&visible(nodes[u],nodes[v])){double d=cost[u]+std::hypot(nodes[u].x-nodes[v].x,nodes[u].y-nodes[v].y);if(d<cost[v]){cost[v]=d;previous[v]=u;}}
 }
 if(!std::isfinite(cost[1]))return fail();Movement m;m.x_=x;m.y_=y;m.speed_=speed;m.near_=near_z;m.far_=far_z;
 for(size_t i=1;i!=0;i=previous[i])m.route_.push_back(nodes[i]);std::reverse(m.route_.begin(),m.route_.end());m.segment();return m;
}
void Movement::extend_exit(int x,int y,int width,int height) {
 // DOS 1ee39 uses the requested anchor and the pre-walk compositor dimensions.
 int right=640-x,bottom=480-y;LogicalPoint target{x,y};
 if(x<right&&x<bottom&&x<y){if(x<=50)target.x=-width*character_scale(y,near_,far_)/10000;}
 else if(right<x&&right<bottom&&right<y){if(right<=50)target.x=640+width*character_scale(y,near_,far_)/10000;}
 else if(y<x&&y<right&&y<bottom){if(y<=50)target.y=-height*character_scale(y,near_,far_)/10000;}
 else if(bottom<=50)target.y=480+height*character_scale(480,near_,far_)/10000;
 route_.back()=target;if(segment_+1==route_.size())segment();
}
void Movement::advance() {
 if(arrived_)return;double speed=std::max(.1,speed_*character_scale(y_,near_,far_)/10000.0);double total=speed+fraction_;int step=int(total+.5);fraction_=total-step;auto to=route_[segment_];
 if(travel_+step>distance_){x_=to.x;y_=to.y;if(++segment_==route_.size())arrived_=true;else segment();return;}
 travel_+=step;int dx=to.x-sx_,dy=to.y-sy_;int ax=std::abs(dx),ay=std::abs(dy),major=std::max(ax,ay),minor=std::min(ax,ay);int side=major?int((int64_t(travel_)*minor+major-minor)/major):0;
 x_=sx_+((dx>0)-(dx<0))*(ax>=ay?travel_:side);y_=sy_+((dy>0)-(dy<0))*(ay>ax?travel_:side);
}
}
