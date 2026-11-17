#include "script/intro_runtime.h"
#include "script/intro_movement.h"
#include "world/movement.h"
#include "world/inventory.h"
#include "save/save.h"
#include "data/hash.h"
#include "data/adb.h"
#include "data/byte_reader.h"
#include "data/grp.h"
#include "data/character.h"
#include "data/font.h"
#include "data/frame_crop.h"
#include "audio/mixer.h"
#include "audio/sample_schedule.h"
#include "audio/cd_player.h"
#include "render/scene_graph.h"
#include "render/text_overlay.h"
#include "render/palette.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <set>
namespace hl {
namespace {
Result<std::vector<uint8_t>> name_bytes(std::string_view text) {
 std::vector<uint8_t> out;
 for(size_t i=0;i<text.size();) {
  auto first=uint8_t(text[i++]);char32_t cp=first;unsigned n=0;
  if(first>=0xc2&&first<=0xdf){cp=first&31;n=1;}else if(first>=0xe0&&first<=0xef){cp=first&15;n=2;}else if(first>=0xf0&&first<=0xf4){cp=first&7;n=3;}else if(first>=128)return DataError{"","save name",i-1,"invalid UTF-8"};
  if(n>text.size()-i)return DataError{"","save name",i-1,"truncated UTF-8"};
  for(unsigned j=0;j<n;++j){auto c=uint8_t(text[i++]);if((c&192)!=128)return DataError{"","save name",i-1,"invalid UTF-8 continuation"};cp=(cp<<6)|(c&63);}
  if((n==1&&cp<128)||(n==2&&cp<2048)||(n==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return DataError{"","save name",i-1,"invalid UTF-8 codepoint"};
  unsigned b=32;for(;b<256&&game_codepoint(uint8_t(b))!=cp;++b){}if(b==256)return DataError{"","save name",i-1,"character outside original font"};out.push_back(uint8_t(b));
 }return out;
}
}
struct IntroRuntime::State {
 struct Object {
  ScriptObject script;std::string identity;uint64_t scene{};int32_t z{10},x{},y{},region_x{},region_y{};bool removed{},initialized{},inventory_only{},fixed_surface{};
  std::optional<Region> region;std::string text,mouse;std::vector<uint8_t> label,name_glyphs;std::optional<IndexedFrame> picture;
  std::map<uint8_t,ScriptHandler> handlers;std::map<std::string,ScriptHandler> use_handlers;
 };
 struct Task {std::unique_ptr<Process> process;Object* owner{};uint64_t scene{};bool inventory_context{};};
 struct Animation {FlcDecoder decoder;IndexedFrame frame;Object* owner{};Process* waiter{};int32_t cycles{},delay{},countdown{};std::string variable;bool phase_controlled{},done{},forever{};int32_t x{},y{},z{};bool attached{};};
 struct Voice {uint64_t id{};Process* waiter{};std::string variable;PcmAudio pcm;float gain{};SampleSchedule schedule;bool phase_controlled{};int32_t priority{100};bool inventory_context{};};
 std::string data_identity;std::vector<nlohmann::json> inputs;
 std::vector<uint8_t> dos_base;std::map<std::string,std::set<std::string>> dos_members;bool rebuilding_scene{};
 std::filesystem::path data;AdbDatabase db;GrpArchive archive;BitmapFont font;Interpreter vm;IntroRuntime* services{};
 std::map<std::string,std::set<std::string>> removed_objects,extra_objects;std::set<std::string> initialized_objects;
 std::map<std::string,std::string> region_overrides;
 std::list<Object> objects;std::array<Task,100> tasks;std::map<Process*,Object*> owners;
 std::list<Animation> animations;std::vector<Voice> voices;Inventory inventory;Mixer mixer{48000,4};CdPlayer cd;std::string selected;std::optional<unsigned> selected_slot;bool inventory_open{};
 std::optional<TextOverlay> dialogue;std::string dialogue_text;uint64_t dialogue_voice{};Process* dialogue_waiter{};
 std::optional<ChoiceText> choices;bool suppress_speech{},speech_loop{};std::optional<int> speech_direction;std::optional<CharacterDefinition> speech_character;unsigned speech_frame{},speech_period{1},speech_countdown{},dialogue_ticks{};
 std::string pending_overlay;bool return_screen{};Process* overlay_waiter{};
 struct NameEditor {Object* owner{};Process* waiter{};int slot{};std::vector<uint8_t> bytes;};
 std::optional<NameEditor> name_editor;std::map<int,std::vector<uint8_t>> save_names;int save_slot{-1},load_slot{-1};
 std::vector<float> audio;uint64_t audio_fraction{},tick{},scene_id{};std::string scene,pending,cursor;int pointer_x{},pointer_y{};int user_locks{},inventory_locks{},character_locks{1};bool quit{},screen_visible{true};
 mutable std::map<std::string,IndexedFrame> static_frames,cursor_frames;mutable std::map<std::string,std::vector<IndexedFrame>> character_frames;
 mutable std::map<std::pair<std::string,unsigned>,CroppedFrame> character_crops;
 mutable Palette identity_palette{};
 mutable std::map<std::tuple<std::string,uint32_t,int,int,int,int,int,int,int>,std::string> palette_identities;
 struct Fade {Palette from,target;unsigned step{};Process* waiter{};};
 std::optional<Fade> fade;bool cooperative_fades{},initial_cooperative_fades{};
 Palette palette{},source_palette{},render_palette{};std::function<Result<void>(unsigned)> palette_presenter;std::map<std::string,std::string> strings;std::map<std::string,int32_t> integers{{"version",1}};uint32_t random_state{1};std::vector<nlohmann::json> trace;
 std::optional<Movement> walk;std::optional<Region> walk_region;Process* arrival_waiter{};unsigned char_frame{};int final_direction{-1};
 std::optional<CharacterDefinition> character;std::string character_id;bool character_visible{};int char_x{},char_y{},char_direction{4},near_z{480},far_z{480};
 Result<std::string> resource(const Process& p,int32_t index)const {
  if(index<0||size_t(index)>=p.object->layout.resources.size())return DataError{"",p.object->name,p.pc,"resource index outside table"};return p.object->layout.resources[size_t(index)].name;
 }
 Result<std::string> alias(std::string_view name)const {auto b=db.read(name);if(!b.has_value())return b.error();return archive_name(b.value(),true);}
 Result<IndexedFrame> frame(std::string_view name)const {
  auto cached=static_frames.find(std::string(name));if(cached!=static_frames.end())return cached->second;auto file=alias(name);if(!file.has_value())return file.error();auto b=archive.read(file.value());if(!b.has_value())return b.error();auto d=FlcDecoder::open(b.value(),file.value());if(!d.has_value())return d.error();auto f=d.value().next();if(!f.has_value())return f.error();static_frames.emplace(std::string(name),*f.value());return *f.value();
 }
 Result<const IndexedFrame*> cursor_frame(const std::string& name)const {
  auto cached=cursor_frames.find(name);if(cached!=cursor_frames.end())return &cached->second;
  auto original=frame(name);if(!original.has_value())return original.error();auto crop=crop_dos_surface(original.value());if(!crop.has_value())return crop.error();const auto& c=crop.value();
  IndexedFrame f;f.logical_width=uint16_t(c.width);f.logical_height=uint16_t(c.height);f.palette=original.value().palette;f.source={original.value().source.resource,original.value().source.frame,int(c.x),int(c.y),int(c.width),int(c.height)};f.indices=std::move(crop.value().indices);
  return &cursor_frames.emplace(name,std::move(f)).first->second;
 }
 Result<Region> region(std::string_view name)const{auto b=db.read(name);if(!b.has_value())return b.error();return load_region(b.value(),name);}
 Result<void> name_surface(Object& owner,const std::vector<uint8_t>& text) {
  auto pixels=render_font(font,text);if(!pixels.has_value())return pixels.error();IndexedFrame f;f.logical_width=400;f.logical_height=16;f.indices.resize(400*16);f.palette=palette;
  for(size_t y=0;y<16;++y)for(size_t x=0;x<text.size()*8;++x)f.indices[y*400+x]=pixels.value()[y*text.size()*8+x]?1:0;owner.picture=std::move(f);owner.fixed_surface=true;owner.name_glyphs=text;return {};
 }
 Result<void> begin_speech_animation(int direction,bool loop) {
  if(direction<0||direction>=8)return DataError{"","character",0,"invalid special animation direction"};
  if(!speech_character){auto b=read_file(data/"JOKES.CHR");if(!b.has_value())return b.error();auto c=load_character(b.value(),"JOKES.CHR");if(!c.has_value())return c.error();speech_character=std::move(c.value());}
  const auto& def=speech_character->directions[size_t(direction)];auto b=archive.read(def.file);if(!b.has_value())return b.error();auto d=FlcDecoder::open(b.value(),def.file);if(!d.has_value())return d.error();auto f=d.value().next();if(!f.has_value())return f.error();
  speech_direction=direction;speech_frame=0;speech_period=unsigned(f.value()->effective_delay.numerator);speech_countdown=speech_period;speech_loop=loop;return {};
 }
 Result<void> run(Process& p,Object& owner) {
  owners[&p]=&owner;auto result=vm.run(p,*services,100000);
  owner.handlers.insert(p.registered_handlers.begin(),p.registered_handlers.end());
  if(!result.has_value())return result.error();return {};
 }
 Result<Process*> spawn(Object& owner,size_t entry) {
  for(auto& t:tasks)if(!t.process||t.process->state==ProcessState::terminated) {
   if(t.process)owners.erase(t.process.get());t.process=std::make_unique<Process>(owner.script);t.process->pc=entry;t.owner=&owner;t.scene=scene_id;t.inventory_context=inventory_open;
   auto r=run(*t.process,owner);if(!r.has_value())return r.error();return t.process.get();
  }return DataError{"",owner.identity,entry,"process table full (100 slots)"};
 }
 Result<Object*> object(std::string_view source,std::string identity={}) {
  if(identity.empty())identity=source;
  for(auto& o:objects)if(o.scene==scene_id&&!o.removed&&o.identity==identity)return &o;
  auto b=db.read(source);if(!b.has_value())return b.error();auto script=load_object(b.value(),source);if(!script.has_value())return script.error();
  objects.push_back(Object{});auto& o=objects.back();o.script=std::move(script.value());o.identity=std::move(identity);o.scene=scene_id;o.inventory_only=inventory_open;
  auto imported=dos_members.find(scene);if(rebuilding_scene&&!inventory_open&&o.identity!=scene&&imported!=dos_members.end()&&!imported->second.contains(o.identity)){o.removed=true;return &o;}
  if(removed_objects[scene].contains(o.identity)){o.removed=true;return &o;}
  auto r=spawn(o,0);if(!r.has_value())return r.error();auto changed=region_overrides.find(o.identity);if(changed!=region_overrides.end()){auto region_value=region(changed->second);if(!region_value.has_value())return region_value.error();o.region=std::move(region_value.value());if(o.identity==scene)walk_region=o.region;}return &o;
 }
 Result<void> transition() {
  for(unsigned n=0;!pending.empty();++n) {
   if(n==100)return DataError{"",scene,0,"scene transition budget exhausted"};
   if(!selected.empty()){auto r=inventory.add(selected);if(!r.has_value())return r.error();selected.clear();}inventory_open=false;
   fade.reset();scene=std::move(pending);pending.clear();++scene_id;mixer.stop_all();cd.stop();animations.clear();voices.clear();static_frames.clear();cursor_frames.clear();palette_identities.clear();dialogue.reset();dialogue_text.clear();dialogue_waiter=nullptr;walk.reset();arrival_waiter=nullptr;walk_region.reset();near_z=far_z=480;
   choices.reset();speech_direction.reset();
   for(auto& task:tasks)task={};owners.clear();objects.clear();
   bool returning=initialized_objects.contains(scene);int x=char_x,y=char_y,direction=char_direction;unsigned frame=char_frame;
   rebuilding_scene=true;auto r=object(scene);if(!r.has_value())return r.error();
   auto members=extra_objects[scene];for(const auto& id:members){auto loaded=object(id);if(!loaded.has_value())return loaded.error();}rebuilding_scene=false;
   // Rebuilding a visited screen must not repeat its first-entry character placement.
   if(returning&&character){char_x=x;char_y=y;char_direction=direction;char_frame=frame;}
  }return {};
 }
};
IntroRuntime::IntroRuntime():state_(std::make_unique<State>()){state_->services=this;}
IntroRuntime::~IntroRuntime()=default;
Result<std::unique_ptr<IntroRuntime>> IntroRuntime::open(const std::filesystem::path& data) {
 auto r=std::unique_ptr<IntroRuntime>(new IntroRuntime);auto& s=*r->state_;s.data=data;
 auto db=AdbDatabase::open(data/"DATA.ADB");if(!db.has_value())return db.error();s.db=std::move(db.value());auto archive=GrpArchive::open(data/"GFX.GRP");if(!archive.has_value())return archive.error();s.archive=std::move(archive.value());
 auto font=read_file(data/"HL.EXE");if(!font.has_value())return font.error();auto f=load_original_font({font.value(),252284});if(!f.has_value())return f.error();s.font=f.value();auto identity=sha256_file(data/"DATA.ADB");if(!identity.has_value())return identity.error();s.data_identity=identity.value();
 s.vm.set_trace([&s](const Process& p,const Instruction& ins){s.trace.push_back({{"tick",s.tick},{"scene",s.scene},{"object",p.object->name},{"offset",ins.offset},{"opcode",ins.opcode},{"stack",p.stack}});});return r;
}
bool IntroRuntime::fading()const{return state_->fade.has_value();}
void IntroRuntime::set_cooperative_fades(bool enabled){
 auto& s=*state_;if(s.scene.empty())s.initial_cooperative_fades=enabled;
 else if(s.cooperative_fades!=enabled)s.inputs.push_back({{"tick",s.tick},{"kind","cooperative_fades"},{"enabled",enabled}});
 s.cooperative_fades=enabled;
}
void IntroRuntime::set_palette_presenter(std::function<Result<void>(unsigned)> presenter){state_->palette_presenter=std::move(presenter);}
Result<void> IntroRuntime::settle() {
 for(unsigned depth=0;depth<100;++depth){auto& s=*state_;
  if(s.save_slot>=0||s.load_slot>=0){int slot=s.save_slot>=0?s.save_slot:s.load_slot;auto path=save_directory_/("slot-"+std::to_string(slot)+".hls");
   if(reconstructing_){if(s.save_slot>=0)return DataError{"",s.scene,0,"slot save inside reconstruction history"};s.load_slot=-1;return {};}
   if(s.save_slot>=0){if(!menu_save_)return DataError{"",s.scene,0,"no allowed gameplay state for slot save"};auto saved=*menu_save_;saved.reconstruction["slot_name"]=s.save_names[slot];std::filesystem::create_directories(save_directory_);auto r=save_game(saved,path);if(!r.has_value())return r;}
   s.save_slot=s.load_slot=-1;if(std::filesystem::exists(path))return load(path);return {};
  }
  if(s.return_screen){s.return_screen=false;if(screens_.empty())return {};auto child=std::move(state_);state_=std::move(screens_.back());screens_.pop_back();auto& parent=*state_;
   parent.vm.reset_globals();for(const auto& [name,value]:child->vm.globals()){auto r=parent.vm.event_global(name,value,true);if(!r.has_value())return r;}
   parent.dos_base=std::move(child->dos_base);parent.dos_members=std::move(child->dos_members);parent.inventory=std::move(child->inventory);parent.removed_objects=std::move(child->removed_objects);parent.extra_objects=std::move(child->extra_objects);parent.region_overrides=std::move(child->region_overrides);parent.strings=std::move(child->strings);parent.integers=std::move(child->integers);parent.inputs=std::move(child->inputs);parent.trace=std::move(child->trace);parent.tick=child->tick;parent.pointer_x=child->pointer_x;parent.pointer_y=child->pointer_y;
   if(parent.overlay_waiter){parent.overlay_waiter->state=ProcessState::ready;parent.overlay_waiter=nullptr;}continue;
  }
  if(!s.pending_overlay.empty()){
   if(screens_.size()>=16)return DataError{"",s.scene,0,"temporary screen nesting exceeds16"};auto name=std::move(s.pending_overlay);s.pending_overlay.clear();auto opened=open(s.data);if(!opened.has_value())return opened.error();auto child=std::move(opened.value()->state_);child->services=this;
   for(const auto& [key,value]:s.vm.globals()){auto r=child->vm.event_global(key,value,true);if(!r.has_value())return r;}
   child->scene=std::move(name);child->pointer_x=s.pointer_x;child->pointer_y=s.pointer_y;child->tick=s.tick;child->inputs=s.inputs;child->trace=s.trace;child->dos_base=s.dos_base;child->dos_members=s.dos_members;child->inventory=s.inventory;child->removed_objects=s.removed_objects;child->extra_objects=s.extra_objects;child->region_overrides=s.region_overrides;child->strings=s.strings;child->integers=s.integers;child->palette=s.palette;child->source_palette=s.source_palette;child->render_palette=s.render_palette;child->palette_presenter=s.palette_presenter;child->cooperative_fades=s.cooperative_fades;child->initial_cooperative_fades=s.initial_cooperative_fades;child->cursor=s.cursor;child->character=s.character;child->character_id=s.character_id;child->char_x=s.char_x;child->char_y=s.char_y;child->char_direction=s.char_direction;
   screens_.push_back(std::move(state_));state_=std::move(child);auto r=state_->object(state_->scene);if(!r.has_value())return r.error();continue;
  }
  auto r=s.transition();if(!r.has_value())return r;if(s.pending_overlay.empty()&&!s.return_screen)return {};
 }
 return DataError{"",state_->scene,0,"temporary screen transition budget exhausted"};
}
Result<void> IntroRuntime::start(){auto& s=*state_;s.scene="main";auto r=s.object("main");if(!r.has_value())return r.error();return settle();}
const std::string& IntroRuntime::scene()const{return state_->scene;}
const std::map<std::string,int32_t>& IntroRuntime::variables()const{return state_->vm.globals();}
bool IntroRuntime::quit_requested()const{return state_->quit;}
bool IntroRuntime::character_loaded()const{return state_->character.has_value();}
const std::vector<nlohmann::json>& IntroRuntime::trace()const{return state_->trace;}
std::span<const float> IntroRuntime::audio()const{return state_->audio;}
bool IntroRuntime::can_save()const {const auto& s=*state_;auto menu=s.strings.find("escape_scr");return (menu==s.strings.end()||s.scene!=menu->second)&&screens_.empty()&&!s.fade&&s.character&&s.inventory_locks<=0&&s.user_locks<=0&&!s.dialogue_waiter&&!s.name_editor&&s.selected.empty()&&!s.inventory_open&&!s.quit;}
Result<SaveState> IntroRuntime::save_state()const {
 const auto& s=*state_;if(!can_save())return DataError{"",s.scene,0,"save not allowed at this point"};if(s.tick>1000000||s.inputs.size()>100000)return DataError{"",s.scene,0,"reconstruction history exceeds bounded save policy"};
 auto saved=SaveState{s.data_identity,{{"cooperative_fades",s.initial_cooperative_fades},{"tick",s.tick},{"events",s.inputs},{"slot_queries",slot_queries_},{"pointer",{s.pointer_x,s.pointer_y}},{"checkpoint",nlohmann::json::parse(canonical_checkpoint(checkpoint()))}}};if(!s.dos_base.empty())saved.reconstruction["dos_base"]=s.dos_base;return saved;
}
Result<void> IntroRuntime::save(const std::filesystem::path& path)const {auto saved=save_state();if(!saved.has_value())return saved.error();return save_game(saved.value(),path);}
Result<std::unique_ptr<IntroRuntime>> IntroRuntime::restore(const std::filesystem::path& data,const std::filesystem::path& path) {
 auto runtime=open(data);if(!runtime.has_value())return runtime.error();auto& g=*runtime.value();std::error_code size_error;auto size=std::filesystem::file_size(path,size_error);if(size_error||size>16*1024*1024+76)return DataError{"","save",0,"missing or invalid save size"};auto bytes=read_file(path);if(!bytes.has_value())return bytes.error();
 if(bytes.value().size()>=4&&bytes.value()[0]==std::byte{0x9a}&&bytes.value()[1]==std::byte{2}&&bytes.value()[2]==std::byte{}&&bytes.value()[3]==std::byte{}){auto restored=g.restore_dos(bytes.value());if(!restored.has_value())return restored.error();return runtime;}
 auto state=load_game(path,g.state_->data_identity);if(!state.has_value())return state.error();
 try {const auto& saved=state.value().reconstruction;auto tick=saved.at("tick").get<uint64_t>();const auto& events=saved.at("events");if(tick>1000000||!events.is_array()||events.size()>100000)return DataError{"","save",0,"invalid reconstruction bounds"};g.slot_queries_=saved.value("slot_queries",std::vector<nlohmann::json>{});if(g.slot_queries_.size()>100000)return DataError{"","save",0,"slot query history exceeds bound"};g.reconstructing_=true;g.set_cooperative_fades(saved.value("cooperative_fades",false));Result<void> r;if(saved.contains("dos_base")){auto base=saved.at("dos_base").get<std::vector<uint8_t>>();r=g.restore_dos(std::as_bytes(std::span(base)));}else r=g.start();if(!r.has_value())return r.error();uint64_t previous=0;
  for(const auto& e:events){auto at=e.at("tick").get<uint64_t>();if(at<previous||at>tick)return DataError{"","save",0,"invalid reconstruction event order"};previous=at;while(g.state_->tick<at){r=g.update();if(!r.has_value())return r.error();}auto kind=e.at("kind").get<std::string>();if(kind=="click")r=g.click(e.at("x").get<int>(),e.at("y").get<int>(),e.at("right").get<bool>());else if(kind=="escape")r=g.escape();else if(kind=="text")r=g.text_input(e.at("text").get<std::string>());else if(kind=="edit_key")r=g.edit_key(e.at("key").get<std::string>());else if(kind=="cooperative_fades"){g.set_cooperative_fades(e.at("enabled").get<bool>());r={};}else if(kind=="pointer")r=g.pointer(e.at("x").get<int>(),e.at("y").get<int>());else return DataError{"","save",0,"unknown reconstruction input"};if(!r.has_value())return r.error();}
  while(g.state_->tick<tick){r=g.update();if(!r.has_value())return r.error();}
  if(saved.contains("pointer")){r=g.pointer(saved.at("pointer").at(0).get<int>(),saved.at("pointer").at(1).get<int>());if(!r.has_value())return r.error();}
  if(g.slot_query_cursor_!=g.slot_queries_.size())return DataError{"","save",0,"unused slot query history"};g.reconstructing_=false;
  if(!g.can_save()||nlohmann::json::parse(canonical_checkpoint(g.checkpoint()))!=saved.at("checkpoint"))return DataError{"","save",0,"reconstructed gameplay state differs from saved state"};
 }catch(const nlohmann::json::exception&){return DataError{"","save",0,"invalid reconstruction fields"};}return runtime;
}
Result<void> IntroRuntime::restore_dos(ByteView bytes) {
 auto parsed=parse_dos_save(bytes);if(!parsed.has_value())return parsed.error();const auto& saved=parsed.value();
 auto started=start();if(!started.has_value())return started;auto& s=*state_;
 s.vm.reset_globals();for(const auto& [name,value]:saved.globals){auto r=s.vm.event_global(name,value,true);if(!r.has_value())return r;}
 s.strings=saved.strings;s.integers=saved.integers;s.inventory=Inventory{};
 // The original stores raw buffers after each NUL; only the item name and count are semantic.
 for(unsigned n=0;n<35;++n){unsigned i=n==0?17:n<=17?n-1:n;const auto& slot=saved.inventory[i];for(uint32_t j=0;j<slot.count;++j){auto added=s.inventory.add(slot.item,i);if(!added.has_value())return added;}}
 s.initialized_objects.clear();s.removed_objects.clear();s.extra_objects.clear();s.region_overrides=saved.regions;
 for(const auto& [scene,snapshot]:saved.scenes){if(!snapshot.region.empty())s.region_overrides[scene]=snapshot.region;if(!snapshot.visited)continue;s.initialized_objects.insert(scene);auto& members=s.dos_members[scene];for(const auto& [id,count]:snapshot.members){s.initialized_objects.insert(id);if(count>0){members.insert(id);s.extra_objects[scene].insert(id);}else s.removed_objects[scene].insert(id);}}
 auto chr=read_file(s.data/saved.character_file);if(!chr.has_value())return chr.error();auto definition=load_character(chr.value(),saved.character_file);if(!definition.has_value())return definition.error();s.character=std::move(definition.value());
 s.user_locks=s.inventory_locks=s.character_locks=0;s.character_visible=saved.character_visible;s.char_x=saved.x;s.char_y=saved.y;s.char_direction=saved.direction;s.char_frame=0;
 s.pending=saved.scene;auto transitioned=settle();if(!transitioned.has_value())return transitioned;
 // Scene declarations rebuild handlers and surfaces; DOS resumes the actor at the saved placement.
 s.char_x=saved.x;s.char_y=saved.y;s.char_direction=saved.direction;s.char_frame=0;s.character_visible=saved.character_visible;s.character_locks=1-saved.movement_depth;
 // DOS 0x1e116 associates an outside actor at the scene's walk-region anchor.
 if(s.character_visible&&s.walk_region&&!s.walk_region->contains(s.char_x,s.char_y)){s.char_x=s.walk_region->x;s.char_y=s.walk_region->y;}
 s.source_palette=s.render_palette=saved.palette;for(size_t i=0;i<256;++i)for(size_t component=0;component<3;++component)s.palette[i][component]=saved.palette[i][component]/4;
 s.tick=0;s.inputs.clear();s.trace.clear();s.dos_base.clear();for(auto b:bytes)s.dos_base.push_back(std::to_integer<uint8_t>(b));return {};
}
Result<StepOutcome> IntroRuntime::invoke(Process& p,const Instruction& ins) {
 auto& s=*state_;auto fail=[&](std::string why)->Result<StepOutcome>{return DataError{"",p.object->name,ins.offset,std::move(why)};};
 const unsigned op=ins.opcode;auto found=s.owners.find(&p);if(found==s.owners.end())return fail("unbound DOS service "+std::to_string(op));auto& o=*found->second;
 unsigned count=0;
 switch(op) {
 case 54:break;case 58:case 60:case 61:case 63:case 64:case 65:case 66:case 68:case 70:case 71:case 73:case 74:case 75:case 81:case 84:case 85:case 86:case 87:case 95:case 96:case 97:case 98:case 103:case 104:case 146:case 106:case 107:case 108:case 112:case 113:case 130:case 141:case 142:case 143:case 144:case 145:case 147:case 148:case 149:case 150:case 164:case 166:case 151:case 152:case 155:case 156:count=1;break;
 case 133:case 134:case 135:case 77:case 79:case 89:case 69:case 72:case 82:case 83:case 91:case 92:case 137:case 138:case 115:case 120:count=2;break;
 case 160:case 161:case 80:case 88:case 90:case 116:case 117:case 121:count=3;break;
 case 67:case 93:case 94:case 99:case 100:case 101:case 102:case 105:case 109:case 110:case 111:case 153:case 154:case 139:case 140:case 157:case 158:case 159:case 162:case 163:case 168:case 169:break;
 default:return fail("unbound DOS service "+std::to_string(op));
 }
 if(p.stack.size()<count)return fail("service stack underflow");
 auto arg=[&](unsigned from_bottom){return p.stack[p.stack.size()-count+from_bottom];};
 auto name=[&](unsigned n)->Result<std::string>{return s.resource(p,arg(n));};
 auto pop=[&]{p.stack.resize(p.stack.size()-count);};
 if(op==151||op==152||op==155||op==156){
  int slot=arg(0);if(slot<0||slot>9)return fail("save slot outside0..9");if(save_directory_.empty()&&!reconstructing_)return fail("save directory is not configured");
  if(op==155||op==156){auto path=save_directory_/("slot-"+std::to_string(slot)+".hls");
   if(!s.save_names.contains(slot)){constexpr std::string_view empty_name="Tato pozice zat\xA1m nem\xA0 jm\x82no";std::vector<uint8_t> bytes(empty_name.begin(),empty_name.end());
    try{if(reconstructing_){if(slot_query_cursor_>=slot_queries_.size()||slot_queries_[slot_query_cursor_].at("slot")!=slot)return fail("slot query history differs");bytes=slot_queries_[slot_query_cursor_++].at("bytes").get<std::vector<uint8_t>>();}
     else {if(std::filesystem::exists(path)){auto saved=load_game(path,s.data_identity);if(!saved.has_value())return saved.error();bytes=saved.value().reconstruction.value("slot_name",bytes);}slot_queries_.push_back({{"slot",slot},{"bytes",bytes}});}
    }catch(const nlohmann::json::exception&){return fail("invalid save slot name");}if(bytes.size()>50)return fail("save slot name exceeds50 glyphs");s.save_names[slot]=std::move(bytes);}
   if(op==156){if(s.name_editor)return fail("save name editor already active");if(!o.picture)return fail("save name editor has no label surface");s.name_editor=State::NameEditor{&o,&p,slot,s.save_names[slot]};p.state=ProcessState::waiting;}
   else {auto r=s.name_surface(o,s.save_names[slot]);if(!r.has_value())return r.error();}
  }else {if(op==151)s.load_slot=slot;else s.save_slot=slot;p.state=ProcessState::terminated;}
  pop();return p.state==ProcessState::waiting?StepOutcome::yielded:p.state==ProcessState::terminated?StepOutcome::terminated:StepOutcome::running;
 }
 if(op==75){auto n=name(0);if(!n.has_value())return n.error();s.pending_overlay=n.value();s.overlay_waiter=&p;p.state=ProcessState::waiting;pop();return StepOutcome::yielded;}
 if(op==159){s.return_screen=true;p.state=ProcessState::terminated;return StepOutcome::terminated;}
 if(op==54) {
  o.handlers[54]={54,ins.next_offset,*ins.branch_target};
  if(!o.initialized&&!s.initialized_objects.contains(o.identity)){o.initialized=true;s.initialized_objects.insert(o.identity);auto r=s.spawn(o,ins.next_offset);if(!r.has_value())return r.error();}return StepOutcome::running;
 }
 if(op==58||op==60) {auto n=name(0);if(!n.has_value())return n.error();if(op==58)o.use_handlers[n.value()]={58,ins.next_offset,*ins.branch_target};else {auto r=s.region(n.value());if(!r.has_value())return r.error();o.region=std::move(r.value());o.handlers[60]={60,ins.next_offset,*ins.branch_target};}pop();return StepOutcome::running;}
 if(op==61||op==63||op==64||op==65||op==68||op==70||op==71||op==74||op==97||op==98||op==103||op==112||op==113) {
  auto n=name(0);if(!n.has_value())return n.error();
  if(op==61||op==103){auto f=s.frame(n.value());if(!f.has_value())return f.error();if(op==103)s.cursor=n.value();else o.mouse=n.value();}
  else if(op==63||op==70){auto r=s.region(n.value());if(!r.has_value())return r.error();if(op==70)s.walk_region=r.value();o.region=std::move(r.value());}
  else if(op==64){auto f=s.frame(n.value());if(!f.has_value())return f.error();o.picture=std::move(f.value());o.fixed_surface=false;}
  else if(op==68){auto b=s.db.read(n.value());if(!b.has_value())return b.error();auto t=decode_game_text(decrypt_game_text(b.value()),GameEncoding::kamenicky);if(!t.has_value())return t.error();o.text=t.value();o.label.clear();for(auto c:decrypt_game_text(b.value())){auto v=std::to_integer<uint8_t>(c);if(!v)break;o.label.push_back(v);}}
  else if(op==71){pop();if(s.dos_members.contains(s.scene))s.dos_members[s.scene].insert(n.value());s.removed_objects[s.scene].erase(n.value());auto r=s.object(n.value());if(!r.has_value())return r.error();if(!s.inventory_open&&!r.value()->removed)s.extra_objects[s.scene].insert(r.value()->identity);return StepOutcome::running;}
  else if(op==74){s.pending=n.value();p.state=ProcessState::terminated;pop();return StepOutcome::terminated;}
  else if(op==97||op==98){auto f=s.frame(n.value());if(!f.has_value())return f.error();s.source_palette=f.value().palette;Palette target;for(size_t i=0;i<256;++i)for(size_t c=0;c<3;++c)target[i][c]=f.value().palette[i][c]/4;
   bool uniform_endpoint=std::all_of(target.begin(),target.end(),[&](const auto& color){return color==target[0];})&&(target[0]==std::array<uint8_t,3>{0,0,0}||target[0]==std::array<uint8_t,3>{63,63,63});if(!uniform_endpoint)s.render_palette=f.value().palette;
   if(op==97)s.palette=target;else if(s.cooperative_fades){
    s.fade=State::Fade{s.palette,target,0,&p};p.state=ProcessState::waiting;pop();return StepOutcome::yielded;
   }else {auto from=s.palette;for(unsigned step=1;step<=64;++step){auto palette=interpolate_palette(from,target,step);if(!palette.has_value())return palette.error();s.palette=palette.value();if(s.palette_presenter){auto shown=s.palette_presenter(step);if(!shown.has_value())return shown.error();}}}
  }
  else if(op==65||op==112){
   if(op==65){if(std::any_of(s.animations.begin(),s.animations.end(),[&](const auto& a){return a.attached&&a.owner==&o&&!a.done;}))return fail("object already has an attached animation");p.animation.cycles=0;p.animation.phase_variable=-2;p.animation.loop=true;p.animation.speed=100;}
   // DOS13372/138fc copy but never consume sample scheduling/manual flags for FLCs.
   if(p.animation.speed<0||p.animation.speed>100000)return fail("animation speed outside bounded intro range");
   auto file=s.alias(n.value());if(!file.has_value())return file.error();auto b=s.archive.read(file.value());if(!b.has_value())return b.error();auto d=FlcDecoder::open(b.value(),file.value());if(!d.has_value())return d.error();auto f=d.value().next();if(!f.has_value())return f.error();
   std::string variable;if(p.animation.phase_variable>=0){auto v=s.resource(p,p.animation.phase_variable);if(!v.has_value())return v.error();variable=v.value();}
   bool blocks=p.animation.phase_variable==-1&&op==112;
   int period=int((f.value()->effective_delay.numerator-1)*uint64_t(p.animation.speed)/100+1);
   s.animations.push_back({std::move(d.value()),std::move(*f.value()),&o,blocks?&p:nullptr,p.animation.cycles,period,period-1,variable,p.animation.phase_controlled,false,p.animation.phase_variable==-2||p.animation.loop,p.animation.x,p.animation.y,p.animation.z,op==65});
   if(blocks)p.state=ProcessState::waiting;
  }
  else if(op==113){auto file=s.alias(n.value());if(!file.has_value())return file.error();auto b=s.archive.read(file.value());if(!b.has_value())return b.error();auto pcm=decode_wav(b.value());if(!pcm.has_value())return pcm.error();auto schedule=SampleSchedule::begin(p.animation.cycles,p.animation.delay,p.animation.periodic,p.animation.random,s.random_state);if(!schedule.has_value())return schedule.error();float gain=std::clamp(p.animation.volume/100.f,0.f,1.f);uint64_t id=0;if(schedule.value().start_now&&!p.animation.phase_controlled){auto voice=s.mixer.play(pcm.value(),gain,0,1,p.animation.sample_priority);if(!voice.has_value())return voice.error();id=voice.value();}bool blocks=p.animation.phase_variable==-1;std::string variable;if(p.animation.phase_variable>=0){auto v=s.resource(p,p.animation.phase_variable);if(!v.has_value())return v.error();variable=v.value();}s.voices.push_back({id,blocks?&p:nullptr,variable,std::move(pcm.value()),gain,schedule.value(),p.animation.phase_controlled,p.animation.sample_priority,s.inventory_open});if(p.animation.phase_controlled&&!variable.empty()){auto r=s.vm.event_global(variable,1);if(!r.has_value())return r.error();}if(blocks)p.state=ProcessState::waiting;}
  pop();return p.state==ProcessState::waiting?StepOutcome::yielded:StepOutcome::running;
 }
 if(op==66){o.z=arg(0);pop();}else if(op==67)o.z=480;
 else if(op==69){s.near_z=arg(0);s.far_z=arg(1);pop();}
 else if(op==72){auto identity=name(0),source=name(1);if(!identity.has_value())return identity.error();if(!source.has_value())return source.error();pop();if(!s.rebuilding_scene&&s.dos_members.contains(s.scene))s.dos_members[s.scene].insert(identity.value());auto r=s.object(source.value(),identity.value());if(!r.has_value())return r.error();}
 else if(op==73){auto n=name(0);if(!n.has_value())return n.error();pop();for(auto& item:s.objects)if(item.scene==s.scene_id&&item.identity==n.value()&&!item.removed){item.removed=true;if(s.dos_members.contains(s.scene))s.dos_members[s.scene].erase(item.identity);if(item.region||s.extra_objects[s.scene].contains(item.identity))s.removed_objects[s.scene].insert(item.identity);s.extra_objects[s.scene].erase(item.identity);for(auto& t:s.tasks)if(t.process&&t.process.get()!=&p&&t.owner==&item)t.process->state=ProcessState::terminated;break;}if(p.state==ProcessState::terminated)return StepOutcome::terminated;}
 else if(op==80){auto id=name(0),file=name(1),script=name(2);if(!id.has_value())return id.error();if(!file.has_value())return file.error();if(!script.has_value())return script.error();auto filename=s.alias(file.value());if(!filename.has_value())return filename.error();auto bytes=read_file(s.data/filename.value());if(!bytes.has_value())return bytes.error();auto chr=load_character(bytes.value(),filename.value());if(!chr.has_value())return chr.error();s.character=std::move(chr.value());s.character_id=id.value();pop();auto r=s.object(script.value());if(!r.has_value())return r.error();}
 else if(op==79){auto id=name(0),region=name(1);if(!id.has_value())return id.error();if(!region.has_value())return region.error();auto r=s.region(region.value());if(!r.has_value())return r.error();auto item=std::find_if(s.objects.begin(),s.objects.end(),[&](const auto& item){return !item.removed&&item.identity==id.value();});if(item!=s.objects.end()){item->region=std::move(r.value());if(item->scene==s.scene_id&&item->identity==s.scene)s.walk_region=item->region;}else {auto b=s.db.read(id.value());if(!b.has_value())return b.error();auto target=load_object(b.value(),id.value());if(!target.has_value())return target.error();}s.region_overrides[id.value()]=region.value();pop();}
 else if(op==83){auto r=s.begin_speech_animation(arg(1),false);if(!r.has_value())return r.error();pop();}
 else if(op==82){auto id=name(0),script=name(1);if(!id.has_value())return id.error();if(!script.has_value())return script.error();if(id.value()!=s.character_id)return fail("character identity is not loaded");pop();auto r=s.object(script.value());if(!r.has_value())return r.error();}
 // DOS2f6c6/2f6ea/2fc94/2fcb4 consume the name but control the singleton.
 else if(op==84||op==85||op==86||op==87){auto id=name(0);if(!id.has_value())return id.error();if(op==84||op==85)s.character_visible=op==85;else s.character_locks+=op==86?1:-1;pop();}
 else if(op==90){auto id=name(0),region=name(1);if(!id.has_value())return id.error();if(!region.has_value())return region.error();if(!s.character)return fail("character is not loaded");auto r=s.region(region.value());if(!r.has_value())return r.error();s.char_x=r.value().x;s.char_y=r.value().y;s.char_direction=arg(2);s.char_frame=0;pop();}
 else if(op==91||op==92){auto id=name(0);if(!id.has_value())return id.error();if(id.value()!=s.character_id)return fail("character identity is not loaded");if(op==91)s.char_direction=arg(1);else {auto target=name(1);if(!target.has_value())return target.error();auto r=s.region(target.value());if(!r.has_value())return r.error();s.char_direction=walk_direction(r.value().x-s.char_x,r.value().y-s.char_y);}s.char_frame=0;pop();}
 else if(op==77){auto id=name(0),text=name(1);if(!id.has_value())return id.error();if(!text.has_value())return text.error();auto b=s.db.read(text.value());if(!b.has_value())return b.error();auto decoded=decode_game_text(decrypt_game_text(b.value()),GameEncoding::kamenicky);if(!decoded.has_value())return decoded.error();for(auto& object:s.objects)if(object.scene==s.scene_id&&object.identity==id.value())object.text=decoded.value();pop();}
 else if(op==88||op==89){
  // DOS2f81f/2f8bb consume the character name but move the singleton at47f84.
  auto id=name(0),target=name(1);if(!id.has_value())return id.error();if(!target.has_value())return target.error();if(!s.character||!s.walk_region)return fail("movement has no character or walk region");
  int exit_width=0,exit_height=0;
  if(op==89){const auto& pose=s.character->directions[s.char_direction];auto bytes=s.archive.read(pose.file);if(!bytes.has_value())return bytes.error();auto decoder=FlcDecoder::open(bytes.value(),pose.file);if(!decoder.has_value())return decoder.error();
   for(unsigned i=0;i<=s.char_frame%pose.offsets.size();++i){auto frame=decoder.value().next();if(!frame.has_value())return frame.error();if(!frame.value())return fail("missing exit pose");auto crop=crop_dos_surface(*frame.value());if(!crop.has_value())return crop.error();exit_width=int(crop.value().width);exit_height=int(crop.value().height);}}
  auto to=s.region(target.value());if(!to.has_value())return to.error();s.final_direction=op==88?arg(2):-1;if(s.final_direction< -1||s.final_direction>=8)return fail("invalid direction");s.char_direction=walk_direction(to.value().x-s.char_x,to.value().y-s.char_y);
  auto destination=walk_target(*s.walk_region,to.value().x,to.value().y);if(!destination.has_value())return destination.error();auto w=Movement::begin(*s.walk_region,s.char_x,s.char_y,destination.value().x,destination.value().y,s.character->directions[s.char_direction].speed,s.near_z,s.far_z);if(!w.has_value())return w.error();if(op==89)w.value().extend_exit(to.value().x,to.value().y,exit_width,exit_height);if(s.arrival_waiter&&s.arrival_waiter!=&p)s.arrival_waiter->state=ProcessState::terminated;s.walk=std::move(w.value());s.char_x=s.walk->x();s.char_y=s.walk->y();s.char_direction=s.walk->direction();s.arrival_waiter=&p;p.state=ProcessState::waiting;pop();return StepOutcome::yielded;
 }
 else if(op==120||op==121) {
  if(s.dialogue_waiter)return fail("overlapping dialogue");auto text=name(1);if(!text.has_value())return text.error();int x=150,y=100;
  if(arg(0)!=-1){auto n=name(0);if(!n.has_value())return n.error();auto r=s.region(n.value());if(!r.has_value())return r.error();x=r.value().x;y=r.value().y;}
  uint32_t mask=0;if(op==121){auto n=name(2);if(!n.has_value())return n.error();auto v=s.vm.globals().find(n.value());if(v==s.vm.globals().end())return fail("choice mask global is undefined");mask=uint32_t(v->second);}
  auto b=s.db.read(text.value());if(!b.has_value())return b.error();auto raw=decrypt_game_text(b.value());std::vector<uint8_t> bytes;for(auto c:raw)bytes.push_back(std::to_integer<uint8_t>(c));
  auto choices=make_choice_text(bytes,mask);if(!choices.has_value())return choices.error();auto overlay=make_text_overlay(s.font,choices.value().text,x,y);if(!overlay.has_value())return overlay.error();
  auto plain=decode_game_text(std::as_bytes(std::span(choices.value().text)),GameEncoding::kamenicky);if(!plain.has_value())return plain.error();s.dialogue_text=plain.value();s.choices=std::move(choices.value());s.dialogue=std::move(overlay.value());s.dialogue_waiter=&p;p.state=ProcessState::waiting;pop();return StepOutcome::yielded;
 }
 else if(op==115||op==116||op==117) {
  if(s.dialogue_waiter)return fail("overlapping intro dialogue");auto text=name(1);if(!text.has_value())return text.error();int x=150,y=100;
  if(arg(0)!=-1){auto n=name(0);if(!n.has_value())return n.error();auto r=s.region(n.value());if(!r.has_value())return r.error();x=r.value().x;y=r.value().y;}
  auto b=s.db.read(text.value());if(!b.has_value())return b.error();auto raw=decrypt_game_text(b.value());auto plain=decode_game_text(raw,GameEncoding::kamenicky);if(!plain.has_value())return plain.error();
  std::vector<uint8_t> bytes;for(auto c:raw)bytes.push_back(std::to_integer<uint8_t>(c));auto overlay=make_text_overlay(s.font,bytes,x,y);if(!overlay.has_value())return overlay.error();
  uint64_t voice=0;s.dialogue_ticks=0;
  if(op==117){auto sound=name(2);if(!sound.has_value())return sound.error();auto file=s.alias(sound.value());if(!file.has_value())return file.error();auto sample=s.archive.read(file.value());if(!sample.has_value())return sample.error();auto pcm=decode_wav(sample.value());if(!pcm.has_value())return pcm.error();auto played=s.mixer.play(pcm.value(),s.integers.at("sound_volume")/100.f,0);if(!played.has_value())return played.error();voice=played.value();}
  else {if(op==116)return fail("filtered timed speech is not yet bound");int speed=s.integers.contains("text_speed")?s.integers.at("text_speed"):70;static constexpr int rates[]={0,631,398,251,158,100,63,40,25,16,10};if(speed<10||speed>100)return fail("invalid text speed");size_t length=std::find(bytes.begin(),bytes.end(),0)-bytes.begin();s.dialogue_ticks=unsigned(std::max(size_t(20),length)*size_t(rates[speed/10])/41+1);}
  auto direction=speech_animation_direction(s.char_direction,s.suppress_speech);if(direction){auto r=s.begin_speech_animation(*direction,true);if(!r.has_value())return r.error();}else if(s.speech_loop)s.speech_direction.reset();
  s.dialogue=std::move(overlay.value());s.dialogue_text=plain.value();s.dialogue_voice=voice;s.dialogue_waiter=&p;p.state=ProcessState::waiting;pop();return StepOutcome::yielded;
 }
 else if(op==95||op==96){auto n=name(0);if(!n.has_value())return n.error();auto v=s.vm.globals().find(n.value());if(v==s.vm.globals().end())return fail("sample control global is undefined");auto r=s.vm.event_global(n.value(),v->second|(op==95?2:4));if(!r.has_value())return r.error();pop();}
 else if(op==93||op==94)s.user_locks+=op==93?1:-1;
 else if(op==99)s.screen_visible=false;else if(op==100)s.screen_visible=true;else if(op==102)s.screen_visible=true;
 else if(op==101)s.inventory.clear();
 else if(op==106){auto n=name(0);if(!n.has_value())return n.error();auto b=s.db.read(n.value());if(!b.has_value())return b.error();auto script=load_object(b.value(),n.value());if(!script.has_value())return script.error();auto added=s.inventory.add(n.value(),s.inventory_open?Inventory::hit(s.pointer_x,s.pointer_y):std::optional<unsigned>{});if(!added.has_value())return added.error();s.removed_objects[s.scene].erase(n.value());s.extra_objects[s.scene].erase(n.value());pop();auto item=s.object(n.value());if(!item.has_value())return item.error();item.value()->inventory_only=true;}
 
 else if(op==104){auto n=name(0);if(!n.has_value())return n.error();s.selected=n.value();s.selected_slot.reset();for(unsigned i=0;i<35;++i)if(s.inventory.slots()[i].item==s.selected){s.selected_slot=i;break;}s.inventory.remove(s.selected);o.inventory_only=true;pop();}
 else if(op==107){auto n=name(0);if(!n.has_value())return n.error();s.inventory.remove(n.value());pop();}
 else if(op==105){s.selected.clear();s.selected_slot.reset();}
 else if(op==162){if(!s.selected.empty()){auto r=s.inventory.add(s.selected,s.selected_slot);if(!r.has_value())return r.error();s.selected.clear();s.selected_slot.reset();}}
 else if(op==108){auto r=s.cd.play(unsigned(arg(0)),s.data/"audio"/("track"+std::to_string(arg(0))+".flac"),false);if(!r.has_value())return r.error();pop();}
 else if(op==109||op==110)s.cd.stop();
 else if(op==111){auto r=s.cd.resume();if(!r.has_value())return r.error();}
 else if(op==130){if(arg(0)<-2)return fail("invalid phase variable");if(arg(0)>=0){auto n=name(0);if(!n.has_value())return n.error();auto registered=s.vm.event_global(n.value(),0,true);if(!registered.has_value())return registered.error();}p.animation.phase_variable=arg(0);pop();}
 else if(op==137||op==138){auto key=name(0);if(!key.has_value())return key.error();if(op==137){std::string value;if(arg(1)!=-1){auto n=name(1);if(!n.has_value())return n.error();value=n.value();}s.strings[key.value()]=value;}else s.integers[key.value()]=arg(1);pop();}
 else if(op==157||op==158)s.inventory_locks+=op==157?1:-1;
 else if(op==168||op==169)s.integers["mouse_locks"]+=op==168?1:-1;
 else if(op==139||op==140){p.stack.push_back(op==139?s.pointer_x:s.pointer_y);}
 else if(op==149||op==150){
  Result<std::string> id=o.identity;if(arg(0)!=-1)id=name(0);if(!id.has_value())return id.error();int position=0;bool found=false;
  for(unsigned i=0;i<35&&!found;++i)if(s.inventory.slots()[i].item==id.value())for(const auto& item:s.objects)if(item.scene==s.scene_id&&!item.removed&&item.identity==id.value()){
   auto f=s.frame(item.mouse);if(!f.has_value())return f.error();auto crop=crop_dos_surface(f.value());if(!crop.has_value())return crop.error();position=op==149?int(i%7)*88+20+(70-int(crop.value().width))/2:int(i/7)*89+27+(71-int(crop.value().height))/2;found=true;break;
  }
  // DOS 0x291fd/0x29249 accept -1 for self and query the cropped surface anchor.
  if(!found)for(const auto& item:s.objects)if(item.scene==s.scene_id&&!item.removed&&item.identity==id.value()&&item.picture){
   auto crop=crop_dos_surface(*item.picture);if(!crop.has_value())return crop.error();position=op==149?item.x+(item.fixed_surface?0:int(crop.value().x)):item.y+(item.fixed_surface?0:int(crop.value().y));break;
  }
  pop();p.stack.push_back(position);
 }
 else if(op==160){Result<std::string> id=o.identity;if(arg(0)!=-1)id=name(0);if(!id.has_value())return id.error();bool found=false;for(auto& item:s.objects)if(item.scene==s.scene_id&&!item.removed&&item.identity==id.value()){if(!item.picture)return fail("object move has no surface");auto crop=crop_dos_surface(*item.picture);if(!crop.has_value())return crop.error();item.x=arg(1)-(item.fixed_surface?0:int(crop.value().x));item.y=arg(2)-(item.fixed_surface?0:int(crop.value().y));found=true;break;}if(!found)return fail("object move target missing");pop();}
 else if(op==154){s.vm.reset_globals();s.dos_members.clear();s.inventory=Inventory{};s.removed_objects.clear();s.extra_objects.clear();s.region_overrides.clear();s.initialized_objects.clear();s.inventory_locks=0;screens_.clear();}
 else if(op==164){auto n=name(0);if(!n.has_value())return n.error();auto i=s.integers.find(n.value());auto value=i==s.integers.end()?0:i->second;pop();p.stack.push_back(value);}
 else if(op==133||op==134||op==135){auto id=name(1);if(!id.has_value())return id.error();std::string screen=s.scene;if(arg(0)!=-1){auto n=name(0);if(!n.has_value())return n.error();screen=n.value();}
  if(op==135){int exists=0;if(screen==s.scene){for(auto& item:s.objects)if(item.scene==s.scene_id&&!item.removed&&(!item.inventory_only||s.inventory_open)&&item.identity==id.value())exists=1;}else if(s.strings.contains("inventory_scr")&&screen==s.strings.at("inventory_scr")){auto items=s.inventory.items();exists=std::find(items.begin(),items.end(),id.value())!=items.end();}else exists=s.extra_objects[screen].contains(id.value())&&!s.removed_objects[screen].contains(id.value());pop();p.stack.push_back(exists);}
  else if(op==133){if(s.dos_members.contains(screen))s.dos_members[screen].insert(id.value());s.removed_objects[screen].erase(id.value());s.extra_objects[screen].insert(id.value());pop();if(screen==s.scene){auto r=s.object(id.value());if(!r.has_value())return r.error();}}
  else {if(s.dos_members.contains(screen))s.dos_members[screen].erase(id.value());s.removed_objects[screen].insert(id.value());s.extra_objects[screen].erase(id.value());if(screen==s.scene)for(auto& item:s.objects)if(!item.removed&&item.identity==id.value()){item.removed=true;for(auto& t:s.tasks)if(t.process&&t.process.get()!=&p&&t.owner==&item)t.process->state=ProcessState::terminated;}pop();}
 }
 else if(op==141||op==142){auto n=name(0);if(!n.has_value())return n.error();auto r=s.region(n.value());if(!r.has_value())return r.error();int value=op==141?r.value().x:r.value().y;pop();p.stack.push_back(value);}
 else if(op==143||op==144||op==145){int value=op==143?(s.speech_direction?int(s.speech_frame):-1):op==144?s.char_x:s.char_y;pop();p.stack.push_back(value);}
 else if(op==147||op==148){std::string id=o.identity;if(arg(0)!=-1){auto n=name(0);if(!n.has_value())return n.error();id=n.value();}int value=0;for(const auto& item:s.objects)if(!item.removed&&item.identity==id&&item.picture){auto crop=crop_dos_surface(*item.picture);if(!crop.has_value())return crop.error();value=item.fixed_surface?0:op==147?int(crop.value().x):int(crop.value().y);break;}pop();p.stack.push_back(value);}
 else if(op==161){std::string id=o.identity;if(arg(0)!=-1){auto n=name(0);if(!n.has_value())return n.error();id=n.value();}auto item=std::find_if(s.objects.begin(),s.objects.end(),[&](const auto& item){return !item.removed&&item.identity==id;});if(item==s.objects.end()||!item->picture)return fail("region offset target has no surface");item->region_x=arg(1);item->region_y=arg(2);pop();}
 else if(op==146){auto n=name(0);if(!n.has_value())return n.error();int v=(s.inventory_open?std::string("14ae"):s.scene).compare(n.value());pop();p.stack.push_back(v);}
 else if(op==166){auto r=dos_random(s.random_state,arg(0));if(!r.has_value())return r.error();pop();p.stack.push_back(r.value());}
 else if(op==153)s.quit=true;
 else if(op==163)s.suppress_speech=true;
 else return fail("unbound DOS service "+std::to_string(op));
 return StepOutcome::running;
}
Result<void> IntroRuntime::update() {
 auto& s=*state_;++s.tick;
 if(!s.fade&&!s.inventory_open&&s.walk&&!s.walk->arrived()){s.walk->advance();s.char_x=s.walk->x();s.char_y=s.walk->y();if(s.char_direction!=s.walk->direction())s.char_frame=0;else ++s.char_frame;s.char_direction=s.walk->direction();if(s.walk->arrived()){if(s.final_direction>=0)s.char_direction=s.final_direction;s.char_frame=0;if(s.arrival_waiter){s.arrival_waiter->state=ProcessState::ready;s.arrival_waiter=nullptr;}}}
 s.audio_fraction+=48000;size_t frames=s.audio_fraction/35;s.audio_fraction%=35;s.audio.resize(frames*2);s.mixer.mix(s.audio);std::vector<float> music(s.audio.size());auto cd=s.cd.mix(music,48000);if(!cd.has_value())return cd;
 for(size_t i=0;i<s.audio.size();++i)s.audio[i]=std::clamp(s.audio[i]+music[i],-1.f,1.f);
 // Keep mixing audio while the script waits for one palette step per update.
 if(s.fade){
  auto& fade=*s.fade;auto palette=interpolate_palette(fade.from,fade.target,++fade.step);if(!palette.has_value())return palette.error();s.palette=palette.value();
  if(s.palette_presenter){auto shown=s.palette_presenter(fade.step);if(!shown.has_value())return shown;}
  if(fade.step<64)return {};
  auto* waiter=fade.waiter;waiter->state=ProcessState::ready;s.fade.reset();
  auto continued=s.run(*waiter,*s.owners.at(waiter));if(!continued.has_value())return continued;return settle();
 }
 if(s.dialogue_ticks)--s.dialogue_ticks;
 if(s.dialogue_waiter&&!s.choices&&!s.dialogue_ticks&&s.mixer.finished(s.dialogue_voice)){s.dialogue_waiter->state=ProcessState::ready;s.dialogue_waiter=nullptr;s.dialogue.reset();s.dialogue_text.clear();if(s.speech_loop)s.speech_direction.reset();}
 if(s.speech_direction&&!--s.speech_countdown){s.speech_countdown=s.speech_period;if(++s.speech_frame>=s.speech_character->directions[size_t(*s.speech_direction)].offsets.size()){s.speech_frame=0;if(!s.speech_loop)s.speech_direction.reset();}}
 for(auto& voice:s.voices)if(voice.inventory_context==s.inventory_open&&!voice.schedule.done){
  bool playing=voice.id&&!s.mixer.finished(voice.id);
  // DOS13adf consumes restart/stop bits from the sample's named phase global.
  if(voice.phase_controlled&&!voice.variable.empty()){
   auto flags=s.vm.globals().at(voice.variable);
   if(flags>1){auto r=s.vm.event_global(voice.variable,1);if(!r.has_value())return r;
    if(flags&2){if(playing)s.mixer.stop(voice.id);auto id=s.mixer.play(voice.pcm,voice.gain,0,1,voice.priority);if(!id.has_value())return id.error();voice.id=id.value();playing=true;}
    if(flags&4){voice.phase_controlled=false;voice.schedule.cycles=1;voice.schedule.countdown=0;}
   }
   if(voice.phase_controlled)continue;
  }
  if(voice.schedule.advance(playing,s.random_state)){auto id=s.mixer.play(voice.pcm,voice.gain,0,1,voice.priority);if(!id.has_value())return id.error();voice.id=id.value();}
  if(voice.schedule.done){if(voice.waiter){voice.waiter->state=ProcessState::ready;voice.waiter=nullptr;}if(!voice.variable.empty()){auto r=s.vm.event_global(voice.variable,0);if(!r.has_value())return r;voice.variable.clear();}}
 }

 for(auto& a:s.animations)if(!s.inventory_open&&!a.done) {
  // A pickup handler can remove itself and still wait for its independent animation.
  if(a.owner->removed&&(a.attached||std::none_of(s.tasks.begin(),s.tasks.end(),[&](const auto& task){return task.owner==a.owner&&task.process&&task.process->state!=ProcessState::terminated;}))){a.done=true;continue;}
  if(a.countdown-->0)continue;a.countdown=a.delay-1;auto next=a.decoder.next();if(!next.has_value())return next.error();
  if(!next.value()) {
   if(a.forever||--a.cycles>0){a.decoder.rewind();next=a.decoder.next();if(!next.has_value())return next.error();}
   else {a.done=true;if(a.waiter){a.waiter->state=ProcessState::ready;a.waiter=nullptr;}if(!a.variable.empty()){auto r=s.vm.event_global(a.variable,-1);if(!r.has_value())return r;}continue;}
  }
  a.frame=std::move(*next.value());if(!a.variable.empty()){auto r=s.vm.event_global(a.variable,int32_t(a.frame.frame_index));if(!r.has_value())return r;}
 }
 for(auto& task:s.tasks)if(task.process&&task.scene==s.scene_id&&(!s.inventory_open||task.inventory_context)&&task.process->state==ProcessState::ready) {
  if(task.process->wait_updates){task.process->wait_updates=std::bit_cast<int32_t>(uint32_t(task.process->wait_updates)-1);continue;}auto r=s.run(*task.process,*task.owner);if(!r.has_value())return r;if(s.fade||!s.pending.empty()||!s.pending_overlay.empty()||s.return_screen)break;
 }
 return settle();
}
bool IntroRuntime::inventory_open()const{return state_->inventory_open;}
const std::string& IntroRuntime::selected_item()const{return state_->selected;}
Result<void> IntroRuntime::click(int x,int y,bool right){
 if(can_save()){auto snapshot=save_state();if(!snapshot.has_value())return snapshot.error();menu_save_=std::move(snapshot.value());}
 auto& s=*state_;if(s.name_editor||s.fade)return {};s.inputs.push_back({{"tick",s.tick},{"kind","click"},{"x",x},{"y",y},{"right",right}});s.pointer_x=x;s.pointer_y=y;if(x<0||x>=640||y<0||y>=480)return DataError{"",s.scene,0,"click outside logical canvas"};
 if(s.dialogue_waiter){if(s.choices){auto id=choice_at(*s.choices,*s.dialogue,x,y);if(!id)return {};if(s.dialogue_waiter->stack.size()>=100)return DataError{"",s.scene,0,"choice result stack overflow"};s.dialogue_waiter->stack.push_back(*id);s.choices.reset();}else s.mixer.stop(s.dialogue_voice);s.dialogue_waiter->state=ProcessState::ready;s.dialogue_waiter=nullptr;s.dialogue.reset();s.dialogue_text.clear();if(s.speech_loop)s.speech_direction.reset();return {};}
 if(s.user_locks>0)return {};
 auto close_inventory=[&]{s.inventory_open=false;for(auto& task:s.tasks)if(task.process&&task.inventory_context)task.process->state=ProcessState::terminated;
  for(const auto& voice:s.voices)if(voice.inventory_context)s.mixer.stop(voice.id);std::erase_if(s.voices,[](const auto& voice){return voice.inventory_context;});};
 auto use_selected=[&](State::Object* target)->Result<void>{
  auto selected=s.selected;auto held=s.object(selected);if(!held.has_value())return held.error();State::Object* owner=held.value();std::optional<ScriptHandler> handler;
  if(target){auto h=target->use_handlers.find(selected);auto fallback=target->handlers.find(57);
   if(h!=target->use_handlers.end()){owner=target;handler=h->second;}
   else if(fallback!=target->handlers.end()){owner=target;handler=fallback->second;}
   else {auto reverse=held.value()->use_handlers.find(target->identity);auto held_fallback=held.value()->handlers.find(57);if(reverse!=held.value()->use_handlers.end())handler=reverse->second;else if(held_fallback!=held.value()->handlers.end())handler=held_fallback->second;}
  }else {auto h=held.value()->handlers.find(56);if(h!=held.value()->handlers.end())handler=h->second;}
  if(handler){auto r=s.spawn(*owner,handler->entry);if(!r.has_value())return r.error();}return settle();
 };
 if(s.inventory_open){auto slot=Inventory::hit(x,y);if(!right||(x<=3&&y<=3)){close_inventory();return {};}
  if(!slot)return {};
  if(*slot==17){auto object=s.object(s.selected.empty()?s.inventory.slots()[17].item:s.selected);if(!object.has_value())return object.error();auto h=object.value()->handlers.find(55);if(h!=object.value()->handlers.end()){auto r=s.spawn(*object.value(),h->second.entry);if(!r.has_value())return r.error();}return {};}
  auto item=s.inventory.slots()[*slot].item;State::Object* target=nullptr;if(!item.empty()){auto object=s.object(item);if(!object.has_value())return object.error();target=object.value();target->inventory_only=true;}
  if(!s.selected.empty())return use_selected(target);
  if(target){s.selected_slot=*slot;s.inventory.remove(item);s.selected=item;}return {};
 }

 State::Object* target=nullptr;
 for(auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&o.region&&o.identity!=s.scene&&o.region->contains(x-o.x-o.region_x,y-o.y-o.region_y))if(!target||o.z<target->z)target=&o;
 if(!s.selected.empty()&&right)return use_selected(target);

 if(target){auto h=target->handlers.find(right?56:s.selected.empty()?55:59);if(h==target->handlers.end()&&!right)h=target->handlers.find(59);
  if(h!=target->handlers.end()){auto result=s.spawn(*target,h->second.entry);if(!result.has_value())return result.error();return settle();}if(right)return {};}
 if(!right&&x<=3&&y<=3&&s.inventory_locks<=0){s.inventory_open=true;for(const auto& id:s.inventory.items()){auto r=s.object(id);if(!r.has_value())return r.error();r.value()->inventory_only=true;}
 bool had=false;for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&o.identity=="14iu")had=true;auto helper=s.object("14iu");if(!helper.has_value())return helper.error();if(had){auto r=s.spawn(*helper.value(),0);if(!r.has_value())return r.error();}return {};}
 if(s.character&&s.character_visible&&s.character_locks<=0&&s.walk_region&&s.walk_region->contains(x,y)){
  s.char_direction=walk_direction(x-s.char_x,y-s.char_y);s.final_direction=-1;auto w=Movement::begin(*s.walk_region,s.char_x,s.char_y,x,y,s.character->directions[s.char_direction].speed,s.near_z,s.far_z);if(!w.has_value())return w.error();if(s.arrival_waiter)s.arrival_waiter->state=ProcessState::terminated;s.walk=std::move(w.value());s.char_x=s.walk->x();s.char_y=s.walk->y();s.arrival_waiter=nullptr;
 }
 return {};
}

bool IntroRuntime::editing_save_name()const{return state_->name_editor.has_value();}
Result<void> IntroRuntime::text_input(std::string_view text){auto& s=*state_;if(!s.name_editor)return {};auto bytes=name_bytes(text);if(!bytes.has_value())return bytes.error();auto& e=*s.name_editor;
 s.inputs.push_back({{"tick",s.tick},{"kind","text"},{"text",text}});auto count=std::min(bytes.value().size(),50-e.bytes.size());e.bytes.insert(e.bytes.end(),bytes.value().begin(),bytes.value().begin()+count);return s.name_surface(*e.owner,e.bytes);
}
Result<void> IntroRuntime::edit_key(std::string_view key){auto& s=*state_;if(!s.name_editor)return {};if(key!="backspace"&&key!="enter"&&key!="escape")return DataError{"","save name",0,"unknown editor key"};auto& e=*s.name_editor;
 if(key!="backspace"&&e.waiter->stack.size()>=100)return DataError{"","save name",0,"editor result stack overflow"};s.inputs.push_back({{"tick",s.tick},{"kind","edit_key"},{"key",key}});
 if(key=="backspace"){if(!e.bytes.empty())e.bytes.pop_back();return s.name_surface(*e.owner,e.bytes);}
 bool accepted=key=="enter"&&!e.bytes.empty();if(accepted)s.save_names[e.slot]=e.bytes;auto r=s.name_surface(*e.owner,s.save_names[e.slot]);if(!r.has_value())return r;
 e.waiter->stack.push_back(accepted?1:0);e.waiter->state=ProcessState::ready;s.name_editor.reset();return {};
}
Result<void> IntroRuntime::escape(){
 if(editing_save_name())return edit_key("escape");
 bool gameplay=can_save();if(gameplay){auto snapshot=save_state();if(!snapshot.has_value())return snapshot.error();menu_save_=std::move(snapshot.value());}
 auto& s=*state_;s.inputs.push_back({{"tick",s.tick},{"kind","escape"}});auto i=s.strings.find("escape_scr");
 if(i==s.strings.end()||i->second.empty())return {};
 if(!screens_.empty()&&(i->second==s.scene||i->second==screens_.back()->scene))s.return_screen=true;
 // The original main menu suspends gameplay even when saving is disallowed.
 else if(i->second!=s.scene){if(gameplay||i->second=="126y")s.pending_overlay=i->second;else s.pending=i->second;}
 return settle();
}
Result<std::vector<uint8_t>> IntroRuntime::rgba()const {auto surfaces=render_surfaces();if(!surfaces.has_value())return surfaces.error();return flatten_surfaces(std::move(surfaces.value()));}
Result<std::vector<uint8_t>> IntroRuntime::indexed()const {std::vector<uint8_t> indices;auto surfaces=render_surfaces_impl(false,&indices);if(!surfaces.has_value())return surfaces.error();return indices;}
Result<void> IntroRuntime::pointer(int x,int y){if(x<0||x>=640||y<0||y>=480)return DataError{"","pointer",0,"intro pointer outside logical canvas"};auto& s=*state_;if(s.inventory_open){nlohmann::json event{{"tick",s.tick},{"kind","pointer"},{"x",x},{"y",y}};if(!s.inputs.empty()&&s.inputs.back().at("kind")=="pointer"&&s.inputs.back().at("tick")==s.tick)s.inputs.back()=std::move(event);else s.inputs.push_back(std::move(event));}s.pointer_x=x;s.pointer_y=y;return {};}
Result<std::vector<RenderSurface>> IntroRuntime::render_surfaces(bool packed)const {
 return render_surfaces_impl(packed,nullptr);
}
Result<std::vector<RenderSurface>> IntroRuntime::render_surfaces_impl(bool packed,std::vector<uint8_t>* indices)const {
 const auto& s=*state_;SceneGraph graph;
#ifndef NDEBUG
 nlohmann::json sprites=nlohmann::json::array();
#endif
 auto add_layer=[&](SceneLayer layer,const std::string& id,const char* kind){
  graph.layers.push_back(layer);
#ifndef NDEBUG
  if(draw_observer_){const auto& f=*layer.frame;sprites.push_back({{"object_id",id},{"resource",f.source.resource},{"frame",f.source.frame},{"bounds",{layer.x,layer.y,f.logical_width,f.logical_height}},{"source_crop",{f.source.x,f.source.y,f.source.width,f.source.height}},{"z",layer.z},{"layer",graph.layers.size()-1},{"kind",kind},{"transparent_index",layer.transparent?nlohmann::json(*layer.transparent):nlohmann::json(nullptr)}});
   if((layer.transparent||std::string_view(kind)=="background")&&std::string_view(kind)!="character"){auto crop=crop_dos_surface(f);if(crop.has_value()&&!crop.value().empty){const auto& c=crop.value();sprites.back()["visible_bounds"]={layer.x+int(c.x),layer.y+int(c.y),c.width,c.height};}}
  }
#endif
 };
 if(packed&&s.identity_palette!=s.render_palette){s.palette_identities.clear();s.identity_palette=s.render_palette;}
 for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&!o.inventory_only&&o.picture)add_layer({&*o.picture,o.x,o.y,o.z,o.z==480?std::optional<uint8_t>{}:std::optional<uint8_t>{0}},o.identity,o.fixed_surface?"text":o.z==480?"background":"static");
 std::list<IndexedFrame> inventory_frames;
 if(s.inventory_open){graph.layers.clear();
#ifndef NDEBUG
 sprites.clear();
#endif
 auto background=s.frame("14af");if(!background.has_value())return background.error();inventory_frames.push_back(std::move(background.value()));
  for(unsigned i=0;i<35;++i){std::string mouse;{auto id=s.inventory.slots()[i].item;if(id.empty())continue;for(const auto& object:s.objects)if(object.identity==id&&!object.mouse.empty()){mouse=object.mouse;break;}if(mouse.empty()){auto b=s.db.read(id);if(!b.has_value())return b.error();auto object=load_object(b.value(),id);if(!object.has_value())return object.error();for(const auto& ins:object.value().instructions)if(ins.opcode==61){auto it=std::find_if(object.value().instructions.begin(),object.value().instructions.end(),[&](const auto& p){return p.next_offset==ins.offset;});if(it!=object.value().instructions.end()&&it->operand>=0&&size_t(it->operand)<object.value().layout.resources.size())mouse=object.value().layout.resources[size_t(it->operand)].name;break;}}}
   if(mouse.empty())return DataError{"","inventory",i,"item has no evidenced cursor image"};auto f=s.cursor_frame(mouse);if(!f.has_value())return f.error();const auto& frame=*f.value();add_layer({&frame,int(i%7)*88+20+(70-frame.logical_width)/2,int(i/7)*89+27+(71-frame.logical_height)/2,10,0},s.inventory.slots()[i].item,"inventory_item");
  }
  add_layer({&inventory_frames.back(),0,0,10,0},"14af","background");
 }
 for(const auto& o:s.objects)if(s.inventory_open&&o.scene==s.scene_id&&!o.removed&&o.inventory_only&&o.picture)add_layer({&*o.picture,o.x,o.y,o.z,0},o.identity,"static");
 std::optional<IndexedFrame> character;
 if(s.character&&s.character_visible&&s.walk_region&&!s.inventory_open) {
  const auto& definition=s.speech_direction?s.speech_character->directions.at(size_t(*s.speech_direction)):s.character->directions.at(size_t(s.char_direction));
  auto& frames=s.character_frames[definition.file];if(frames.empty()){auto bytes=s.archive.read(definition.file);if(!bytes.has_value())return bytes.error();auto decoder=FlcDecoder::open(bytes.value(),definition.file);if(!decoder.has_value())return decoder.error();if(definition.offsets.size()>128)return DataError{"",definition.file,0,"character sequence exceeds M3 cache bound"};for(size_t i=0;i<definition.offsets.size();++i){auto f=decoder.value().next();if(!f.has_value())return f.error();if(!f.value())return DataError{"",definition.file,i,"CHR phases exceed FLC frame count"};frames.push_back(std::move(*f.value()));}}
  unsigned phase=(s.speech_direction?s.speech_frame:s.char_frame)%unsigned(definition.offsets.size());const auto& f=frames[phase];int scale=character_scale(s.char_y,s.near_z,s.far_z);
  if(scale>0){auto key=std::pair{definition.file,phase};auto crop=s.character_crops.find(key);if(crop==s.character_crops.end()){auto c=crop_dos_surface(f);if(!c.has_value())return c.error();crop=s.character_crops.emplace(key,std::move(c.value())).first;}auto origin_key=std::pair{definition.file,0u};auto origin=s.character_crops.find(origin_key);if(origin==s.character_crops.end()){auto c=crop_dos_surface(frames.front());if(!c.has_value())return c.error();origin=s.character_crops.emplace(origin_key,std::move(c.value())).first;}auto placed=place_character_frame(f,crop->second,definition.offsets[phase],s.char_x,s.char_y,scale,int(origin->second.width),int(origin->second.height));if(!placed.has_value())return placed.error();character=std::move(placed.value().frame);add_layer({&*character,placed.value().x,placed.value().y,480-s.char_y,0},s.character_id,"character");}
 }
 for(const auto& a:s.animations)if(!a.done&&!s.inventory_open)add_layer({&a.frame,a.attached?a.owner->x:a.x,a.attached?a.owner->y:a.y,a.attached?a.owner->z:a.z,0},a.owner->identity,"animation");
 auto mouse_lock=s.integers.find("mouse_locks");
 const State::Object* hover=nullptr;
 if(!s.inventory_open){for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&o.identity!=s.scene&&o.region&&o.region->contains(s.pointer_x-o.x-o.region_x,s.pointer_y-o.y-o.region_y))if(!hover||o.z<hover->z)hover=&o;}
 else {auto slot=Inventory::hit(s.pointer_x,s.pointer_y);if(slot){auto id=s.inventory.slots()[*slot].item;for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&o.identity==id)hover=&o;}}
 std::string cursor_name=!s.inventory_open&&hover&&!hover->mouse.empty()?hover->mouse:s.cursor;if(!s.selected.empty()){for(const auto& object:s.objects)if(object.scene==s.scene_id&&object.identity==s.selected&&!object.mouse.empty())cursor_name=object.mouse;}
 if(!cursor_name.empty()&&(mouse_lock==s.integers.end()||mouse_lock->second<=0)) {auto f=s.cursor_frame(cursor_name);if(!f.has_value())return f.error();const auto* cursor=f.value();add_layer({cursor,s.pointer_x-(s.selected.empty()?0:cursor->logical_width/2),s.pointer_y-(s.selected.empty()?0:cursor->logical_height/2),-100,0},s.selected.empty()?cursor_name:s.selected,"cursor");}
 if(indices){auto canvas=graph.compose();if(!canvas.has_value())return canvas.error();*indices=std::move(canvas.value());}
 std::vector<RenderSurface> out;
 for(const auto& layer:graph.layers) {
  const auto& f=*layer.frame;RenderSurface surface{layer.x,layer.y,layer.z,f.logical_width,f.logical_height,{}};surface.source=f.source;surface.opaque_base=!layer.transparent;
  if(packed&&surface.source.resource.empty())for(const auto& o:s.objects)if(o.fixed_surface&&o.picture&&&*o.picture==&f){for(size_t i=0;i<o.name_glyphs.size();++i)surface.glyphs.push_back({o.name_glyphs[i],layer.x+int(i)*8,layer.y});surface.text_color=s.render_palette[1];surface.dos_text_clip=true;}
  if(!surface.glyphs.empty()){surface.rgba.resize(size_t(surface.width)*surface.height*4);out.push_back(std::move(surface));continue;}
  if(packed&&!surface.source.resource.empty()){auto key=std::tuple{f.source.resource,f.source.frame,f.source.x,f.source.y,f.source.width,f.source.height,int(f.logical_width),int(f.logical_height),layer.transparent?int(*layer.transparent):-1};auto identity=s.palette_identities.find(key);if(identity==s.palette_identities.end()){auto value=frame_palette_identity(f,s.render_palette,layer.transparent);if(!value.has_value())return value.error();identity=s.palette_identities.emplace(std::move(key),std::move(value.value())).first;}surface.source.palette=identity->second;out.push_back(std::move(surface));continue;}surface.rgba.reserve(f.indices.size()*4);
  size_t pixel=0;for(auto index:f.indices){for(auto v:packed?s.render_palette[index]:s.palette[index])surface.rgba.push_back(packed?v:s.screen_visible?expand_dac(v):0);
   int64_t x=int64_t(layer.x)+int64_t(pixel%f.logical_width),y=int64_t(layer.y)+int64_t(pixel/f.logical_width);++pixel;
   bool clipped=layer.transparent&&((int64_t(layer.x)+f.logical_width>640&&x>=639)||(int64_t(layer.y)+f.logical_height>480&&y>=479));
   surface.rgba.push_back(clipped||(layer.transparent&&index==*layer.transparent)?0:255);
  }out.push_back(std::move(surface));
 }
 if(hover&&!hover->label.empty()&&!s.dialogue){auto pixels=render_font(s.font,hover->label);if(!pixels.has_value())return pixels.error();int width=int(hover->label.size())*8;RenderSurface label{320-int((hover->label.size()+1)/2)*8,462,-90,width,16,{}};size_t p=0;for(auto bit:pixels.value()){if(indices&&bit){int x=label.x+int(p%width),y=label.y+int(p/width);if(x>=0&&x<640&&y>=0&&y<480)(*indices)[size_t(y)*640+x]=1;}++p;for(auto v:packed?s.render_palette[1]:s.palette[1])label.rgba.push_back(packed?v:expand_dac(v));label.rgba.push_back(!packed&&bit?255:0);}if(packed){label.text_color=s.render_palette[1];for(size_t i=0;i<hover->label.size();++i)label.glyphs.push_back({hover->label[i],label.x+int(i)*8,label.y});}out.push_back(std::move(label));}
 if(s.dialogue) {
  const auto& t=*s.dialogue;auto canvas=graph.compose(t.x,t.y,t.width,t.height);if(!canvas.has_value())return canvas.error();RenderSurface text{t.x,t.y,-8,t.width,t.height,{}};text.rgba.reserve(size_t(t.width)*t.height*4);
  for(int y=0;y<t.height;++y)for(int x=0;x<t.width;++x){uint8_t pixel=canvas.value()[size_t(y)*t.width+x];auto index=!packed&&t.mask[size_t(y)*t.width+x]?1:dialogue_shade_source_index(s.source_palette,pixel);if(indices&&t.x+x>=0&&t.x+x<640&&t.y+y>=0&&t.y+y<480)(*indices)[size_t(t.y+y)*640+t.x+x]=uint8_t(index);for(auto v:packed?s.render_palette[index]:s.palette[index])text.rgba.push_back(packed?v:expand_dac(v));text.rgba.push_back(255);}if(packed){text.glyphs=t.glyphs;text.text_color=s.render_palette[1];}out.push_back(std::move(text));
 }
 if(indices){
  auto cursor=std::find_if(graph.layers.begin(),graph.layers.end(),[](const auto& layer){return layer.z==-100;});
  if(cursor!=graph.layers.end()){IndexedFrame base;base.logical_width=640;base.logical_height=480;base.indices=std::move(*indices);SceneGraph complete;complete.layers={{&base,0,0,INT32_MAX,{}},*cursor};auto composed=complete.compose();if(!composed.has_value())return composed.error();*indices=std::move(composed.value());}
 }
#ifndef NDEBUG
 if(draw_observer_){
  for(size_t i=sprites.size();i<out.size();++i){const auto& surface=out[i];sprites.push_back({{"object_id",surface.z==-90?"hover":"dialogue"},{"resource",surface.source.resource},{"frame",surface.source.frame},{"bounds",{surface.x,surface.y,surface.width,surface.height}},{"z",surface.z},{"layer",i},{"kind","text"}});
   if(surface.z==-90){std::vector<uint8_t> mask(640*480);int left=640,top=480,right=-1,bottom=-1;
    for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x){int cx=surface.x+x,cy=surface.y+y;if(cx<0||cx>=640||cy<0||cy>=480||!surface.rgba[(size_t(y)*surface.width+x)*4+3])continue;mask[size_t(cy)*640+cx]=1;left=std::min(left,cx);top=std::min(top,cy);right=std::max(right,cx);bottom=std::max(bottom,cy);}
    sprites.back()["mask_sha256"]=sha256(std::as_bytes(std::span(mask)));sprites.back()["mask_bounds"]=right<left?nlohmann::json(nullptr):nlohmann::json::array({left,top,right-left+1,bottom-top+1});
   }
  }
  nlohmann::json slots=nlohmann::json::array();for(size_t i=0;i<s.inventory.slots().size();++i){const auto& slot=s.inventory.slots()[i];slots.push_back({{"slot",i},{"item",slot.item},{"count",slot.count}});}
  draw_observer_({{"tick",s.tick},{"scene",s.scene},{"inventory_open",s.inventory_open},{"inventory",s.inventory.items()},{"inventory_slots",std::move(slots)},{"selected",s.selected},{"palette_dac",s.palette},{"screen_visible",s.screen_visible},{"packed",packed},{"sprites",std::move(sprites)}});
 }
#endif
 return out;
}
PaletteView IntroRuntime::palette_view()const{return {state_->render_palette,state_->palette,state_->screen_visible};}
std::vector<LogicalPoint> IntroRuntime::active_areas()const {
 const auto& s=*state_;std::vector<LogicalPoint> out;
 if(s.inventory_open||s.user_locks>0||s.dialogue_waiter||s.name_editor)return out;
 for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed&&!o.inventory_only&&o.identity!=s.scene&&o.region&&(o.handlers.contains(55)||o.handlers.contains(56)||o.handlers.contains(59)||!o.use_handlers.empty())){
  int left=640,top=480,right=-1,bottom=-1;ByteReader r(o.region->shape);
  for(unsigned c=0;c<o.region->flags;++c){auto n=r.u16le().value();for(unsigned i=0;i<n;++i){int x=std::bit_cast<int16_t>(r.u16le().value())+o.x+o.region_x,y=std::bit_cast<int16_t>(r.u16le().value())+o.y+o.region_y;r.u16le();left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}}
  left=std::max(0,left);top=std::max(0,top);right=std::min(639,right);bottom=std::min(479,bottom);if(left>right||top>bottom)continue;
  auto enabled=[&](int x,int y){return o.region->contains(x-o.x-o.region_x,y-o.y-o.region_y);};
  int x=(left+right)/2,y=(top+bottom)/2;if(enabled(x,y)){out.push_back({x,y});continue;}
  bool found=false;for(int py=top;py<=bottom&&!found;++py)for(int px=left;px<=right;++px)if(enabled(px,py)){out.push_back({px,py});found=true;break;}
 }
 return out;
}
nlohmann::json IntroRuntime::geometry()const {
 const auto& s=*state_;nlohmann::json j{{"scene",s.scene},{"active_processes",0},{"objects",nlohmann::json::array()}};
 for(const auto& task:s.tasks)if(task.process&&task.process->state!=ProcessState::terminated)j["active_processes"]=j["active_processes"].get<int>()+1;
 for(const auto& o:s.objects)if(o.scene==s.scene_id&&!o.removed){nlohmann::json item{{"id",o.identity},{"z",o.z},{"anchor",{o.x,o.y}},{"inventory_only",o.inventory_only},{"inventory_active",s.inventory_open&&(o.inventory_only||std::any_of(s.tasks.begin(),s.tasks.end(),[&](const auto& task){return task.owner==&o&&task.inventory_context;}))},{"region_offset",{o.region_x,o.region_y}},{"contours",nlohmann::json::array()}};if(o.region){ByteReader r(o.region->shape);for(unsigned c=0;c<o.region->flags;++c){auto n=r.u16le().value();nlohmann::json points=nlohmann::json::array();for(unsigned i=0;i<n;++i){auto x=r.u16le().value(),y=r.u16le().value();r.u16le();points.push_back({std::bit_cast<int16_t>(x),std::bit_cast<int16_t>(y)});}item["contours"].push_back(points);}}j["objects"].push_back(item);}return j;
}
Result<void> IntroRuntime::load(const std::filesystem::path& path){auto restored=restore(state_->data,path);if(!restored.has_value())return restored.error();auto cooperative=state_->cooperative_fades;auto presenter=std::move(state_->palette_presenter);state_.swap(restored.value()->state_);slot_queries_=std::move(restored.value()->slot_queries_);slot_query_cursor_=0;menu_save_.reset();screens_.clear();state_->services=this;state_->palette_presenter=std::move(presenter);set_cooperative_fades(cooperative);return {};}
Checkpoint IntroRuntime::checkpoint()const {
 const auto& s=*state_;Checkpoint c{s.tick,s.scene};c.variables=s.vm.globals();c.inventory=s.inventory.items();c.active_dialogue=s.dialogue?nlohmann::json(s.dialogue_text):nlohmann::json(nullptr);c.animation_positions=nlohmann::json::array();for(const auto& a:s.animations)if(!a.done)c.animation_positions.push_back({{"resource",a.decoder.resource()},{"frame",a.frame.frame_index}});if(s.character&&s.character_visible){const auto& definition=s.speech_direction?s.speech_character->directions.at(size_t(*s.speech_direction)):s.character->directions.at(size_t(s.char_direction));c.animation_positions.push_back({{"character",s.character_id},{"resource",definition.file},{"frame",(s.speech_direction?s.speech_frame:s.char_frame)%definition.offsets.size()},{"x",s.char_x},{"y",s.char_y},{"direction",s.char_direction},{"moving",s.walk&&!s.walk->arrived()}});}
 c.audio_positions={{"track",s.cd.track()},{"sample",s.cd.position()},{"playing",s.cd.playing()},{"voices",nlohmann::json::array()}};
 for(const auto& voice:s.voices)if(!s.mixer.finished(voice.id))c.audio_positions["voices"].push_back({{"id",voice.id},{"sample",s.mixer.position(voice.id)},{"kind","sample"}});
 if(s.dialogue)c.audio_positions["voices"].push_back({{"id",s.dialogue_voice},{"sample",s.mixer.position(s.dialogue_voice)},{"kind","dialogue"}});return c;
}
}
