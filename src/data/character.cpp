#include "data/character.h"
#include "data/byte_reader.h"
#include "data/grp.h"
#include <bit>
namespace hl {
Result<CharacterDefinition> load_character(ByteView bytes,std::string_view name) {
 auto fail=[&](size_t at,std::string why)->Result<CharacterDefinition>{return DataError{"",std::string(name),at,std::move(why)};};
 ByteReader r(bytes);CharacterDefinition out;std::array<bool,8> seen{};
 // DOS 0x1f471: six-byte length/direction header, twenty-byte definition, signed offsets.
 for(unsigned record=0;record<8;++record) {
  size_t at=r.position();auto size=r.u32le();auto dir=r.u16le();
  if(!dir.has_value()||!size.has_value())return fail(at,"truncated CHR record header");
  if(dir.value()>=8||seen[dir.value()])return fail(at,"invalid or duplicate CHR direction");
  if(size.value()<24||size.value()>r.remaining())return fail(at,"CHR record outside file");
  auto count=r.u16le().value(),speed=r.u16le().value();auto filename=r.take(16).value();
  if(!count||size.value()!=20+uint32_t(count)*4||!speed)return fail(at,"invalid CHR count/speed/size");
  auto file=archive_name(filename,true);if(!file.has_value())return fail(at+10,"invalid CHR filename");
  if(!ascii_key(file.value()).ends_with(".flc"))return fail(at+10,"CHR source is not FLC");
  auto& d=out.directions[dir.value()];seen[dir.value()]=true;d.file=file.value();d.speed=std::bit_cast<int16_t>(speed);
  for(unsigned i=0;i<count;++i){auto x=r.u16le().value(),y=r.u16le().value();d.offsets.push_back({std::bit_cast<int16_t>(x),std::bit_cast<int16_t>(y)});}
 }
 if(r.remaining())return fail(r.position(),"unexpected CHR tail");return out;
}
bool Region::contains(int x,int y)const {
 if(x==this->x&&y==this->y)return true;
 if(shape.empty())return true;ByteReader r(shape);
 for(unsigned contour=0;contour<flags;++contour){
  auto count=r.u16le().value();std::vector<CharacterOffset> points;
  for(unsigned i=0;i<count;++i){auto px=r.u16le().value(),py=r.u16le().value();r.u16le();points.push_back({std::bit_cast<int16_t>(px),std::bit_cast<int16_t>(py)});}
  auto side=[&](int px,int py){bool inside=false;for(size_t i=0,j=points.size()-1;i<points.size();j=i++){auto a=points[j],b=points[i];if((a.y>py)!=(b.y>py)&&double(px)<double(b.x-a.x)*(py-a.y)/(b.y-a.y)+a.x)inside=!inside;}return inside;};
  for(auto p:points)if(p.x==x&&p.y==y)return false;
  if(side(x,y)!=side(this->x,this->y))return false;
 }return true;
}

Result<Region> load_region(ByteView bytes,std::string_view name) {
 if(bytes.size()<38)return DataError{"",std::string(name),0,"truncated region"};
 auto label=archive_name(bytes.first(32),true);if(!label.has_value())return label.error();
 ByteReader r(bytes.subspan(32));auto x=r.u16le().value(),y=r.u16le().value(),flags=r.u16le().value();
 ByteReader shape(bytes.subspan(38));
 for(unsigned contour=0;contour<flags;++contour) {
  auto count=shape.u16le();if(!count.has_value()||count.value()<1||count.value()>1000||shape.remaining()<size_t(count.value())*6)return DataError{"",std::string(name),38+shape.position(),"invalid region polygon extent"};shape.take(size_t(count.value())*6);
 }
 if(shape.remaining())return DataError{"",std::string(name),38+shape.position(),"unexpected region polygon tail"};
 return Region{label.value(),std::bit_cast<int16_t>(x),std::bit_cast<int16_t>(y),flags,{bytes.begin()+38,bytes.end()}};
}
}
