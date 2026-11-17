#include "script/intro_movement.h"
#include <algorithm>
#include <cmath>
namespace hl {
int character_scale(int y,int near_z,int far_z) {
 int64_t z=480-int64_t(y);if(z<=near_z)return 10000;if(z>=far_z)return 0;return int((far_z-z)*10000/(int64_t(far_z)-near_z));
}
Result<IntroWalk> IntroWalk::begin(const Region& area,int x,int y,int tx,int ty,int speed,int near_z,int far_z) {
 if(!area.contains(x,y)||!area.contains(tx,ty)||speed<=0||speed>=10||near_z>=far_z)return DataError{"","intro movement",0,"invalid intro walk bounds/speed/planes"};
 // The intro uses one axis-aligned segment in a rectangular region. Other paths belong to M3.
 if(area.shape.size()!=26||(x!=tx&&y!=ty))return DataError{"","intro movement",0,"movement outside evidenced intro segment scope"};
 IntroWalk w;w.x_=w.start_x_=x;w.y_=w.start_y_=y;w.target_x_=tx;w.target_y_=ty;w.distance_=std::max(std::abs(tx-x),std::abs(ty-y));w.speed_=speed;w.near_=near_z;w.far_=far_z;return w;
}
void IntroWalk::advance() {
 if(arrived_)return;double speed=std::max(.1,speed_*character_scale(y_,near_,far_)/10000.0);double total=speed+fraction_;int step=int(total+.5);fraction_=total-step;
 if(travel_+step>distance_){x_=target_x_;y_=target_y_;arrived_=true;return;}
 travel_+=step;int dx=(target_x_>start_x_)-(target_x_<start_x_),dy=(target_y_>start_y_)-(target_y_<start_y_);x_=start_x_+travel_*dx;y_=start_y_+travel_*dy;
}
}
