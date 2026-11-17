#pragma once
#include "data/character.h"
namespace hl {
int character_scale(int y,int near_z,int far_z);
class IntroWalk {
 int x_{},y_{},target_x_{},target_y_{},start_x_{},start_y_{},distance_{},travel_{},speed_{},near_{},far_{};double fraction_{};bool arrived_{};
public:
 static Result<IntroWalk> begin(const Region& area,int x,int y,int target_x,int target_y,int speed,int near_z,int far_z);
 void advance();
 int x()const{return x_;}int y()const{return y_;}bool arrived()const{return arrived_;}
};
}
