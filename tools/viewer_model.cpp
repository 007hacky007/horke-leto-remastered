#include "viewer.h"
#include "data/grp.h"
#include <set>
namespace hl {
Result<void> validate_viewer_canvas(uint32_t w,uint32_t h,std::string resource){if(w!=640||h!=480)return DataError{"",std::move(resource),8,"viewer requires a 640x480 logical canvas"};return {};}

Result<ViewerModel> ViewerModel::create(std::vector<ViewerResource> resources){std::set<std::string> names;if(resources.empty())return DataError{"","viewer",0,"no resources"};for(auto& r:resources)if(!r.frames||!names.insert(ascii_key(r.name)).second)return DataError{"",r.name,0,"empty or duplicate viewer resource"};ViewerModel out;out.resources_=std::move(resources);return out;}
void ViewerModel::advance(){if(++frame_==current().frames){frame_=0;resource_=(resource_+1)%resources_.size();}}
void ViewerModel::previous(){if(frame_)--frame_;else{resource_=(resource_+resources_.size()-1)%resources_.size();frame_=current().frames-1;}}
void ViewerModel::resource(int direction){resource_=(resource_+resources_.size()+(direction<0?-1:1))%resources_.size();frame_=0;}
Result<void> ViewerModel::seek(std::string_view name,uint32_t frame){for(size_t i=0;i<resources_.size();++i)if(ascii_key(name)==ascii_key(resources_[i].name)){if(frame>=resources_[i].frames)return DataError{"",std::string(name),frame,"viewer frame outside resource"};resource_=i;frame_=frame;return {};}return DataError{"",std::string(name),frame,"viewer resource not found"};}
}
