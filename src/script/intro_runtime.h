#pragma once
#include "script/interpreter.h"
#include "debug/checkpoint.h"
#include "render/scene_graph.h"
#include "render/viewport.h"
#include "render/palette.h"
#include "save/save.h"
#include <filesystem>
#include <memory>
namespace hl {
class IntroRuntime:public EngineServices {
 struct State;std::unique_ptr<State> state_;
 std::vector<std::unique_ptr<State>> screens_;
 std::filesystem::path save_directory_;
 std::optional<SaveState> menu_save_;
 std::vector<nlohmann::json> slot_queries_;
 // Unversioned save events retain the released input behavior during replay.
 size_t slot_query_cursor_{};bool reconstructing_{},legacy_input_{};
#ifndef NDEBUG
 std::function<void(const nlohmann::json&)> draw_observer_;
#endif
 Result<SaveState> save_state()const;
 Result<void> restore_dos(ByteView);
 Result<std::vector<RenderSurface>> render_surfaces_impl(bool packed,std::vector<uint8_t>* indices)const;
 Result<void> settle();
 IntroRuntime();
public:
 ~IntroRuntime();
 static Result<std::unique_ptr<IntroRuntime>> open(const std::filesystem::path& data);
 void set_cooperative_fades(bool enabled);
 bool fading()const;
 void set_palette_presenter(std::function<Result<void>(unsigned)> presenter);
 Result<void> start();
 bool can_save()const;
 void set_save_directory(std::filesystem::path path){save_directory_=std::move(path);}
 bool editing_save_name()const;
 Result<void> text_input(std::string_view);
 Result<void> edit_key(std::string_view);
 Result<void> save(const std::filesystem::path&)const;
 static Result<std::unique_ptr<IntroRuntime>> restore(const std::filesystem::path& data,const std::filesystem::path& save);
 Result<void> update();
 Result<void> click(int x,int y,bool right=false);
 Result<void> escape();
 bool inventory_open()const;
 const std::string& selected_item()const;
 Result<std::vector<uint8_t>> rgba()const;
 Result<std::vector<uint8_t>> indexed()const;
 Result<std::vector<RenderSurface>> render_surfaces(bool packed=false)const;
#ifndef NDEBUG
 void set_draw_observer(std::function<void(const nlohmann::json&)> observer){draw_observer_=std::move(observer);}
#endif
 Result<void> pointer(int x,int y);
 std::span<const float> audio()const;
 Checkpoint checkpoint()const;
 nlohmann::json geometry()const;
 std::vector<LogicalPoint> active_areas()const;
 PaletteView palette_view()const;
 Result<void> load(const std::filesystem::path&);
 const std::vector<nlohmann::json>& trace()const;
 bool character_loaded()const;bool quit_requested()const;
 const std::string& scene()const;
 const std::map<std::string,int32_t>& variables()const;
 Result<StepOutcome> invoke(Process&,const Instruction&)override;
};
}
