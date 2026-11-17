#pragma once
#include "data/character.h"
#include "render/viewport.h"
namespace hl {
int walk_direction(int dx,int dy);
Result<LogicalPoint> walk_target(const Region&,int x,int y);
class Movement {
 std::vector<LogicalPoint> route_;size_t segment_{};int x_{},y_{},sx_{},sy_{},distance_{},travel_{},speed_{},near_{},far_{};double fraction_{};bool arrived_{};
 void segment();
public:
 static Result<Movement> begin(const Region&,int x,int y,int tx,int ty,int speed,int near_z,int far_z);
 void extend_exit(int x,int y,int width,int height);
 void advance();int x()const{return x_;}int y()const{return y_;}bool arrived()const{return arrived_;}
 int direction()const{auto p=route_[std::min(segment_,route_.size()-1)];return walk_direction(p.x-sx_,p.y-sy_);}
 const auto& waypoints()const{return route_;}
};
}
