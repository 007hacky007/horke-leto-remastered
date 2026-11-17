#pragma once
#include "data/result.h"
#include <vector>
#include <cstdint>
namespace hl {
Result<void> validate_viewer_canvas(uint32_t width,uint32_t height,std::string resource);
struct ViewerResource {std::string name;uint32_t frames{};};
class ViewerModel {
 std::vector<ViewerResource> resources_;size_t resource_{};uint32_t frame_{};
public:
 static Result<ViewerModel> create(std::vector<ViewerResource>);
 const ViewerResource& current()const{return resources_[resource_];}
 uint32_t frame()const{return frame_;}
 void advance();void previous();void resource(int direction);
 Result<void> seek(std::string_view,uint32_t frame);
};
}
int viewer_command(int argc,char** argv);
