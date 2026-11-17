#include "platform/app.h"
#include <charconv>
#include "render/renderer_policy.h"
namespace hl {
Result<AppOptions> parse_app_options(std::span<const std::string_view> args) {
    AppOptions options;
    auto error = [](std::size_t i, std::string reason) -> Result<AppOptions> {
        return DataError{"arguments", "", i, std::move(reason)};
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--smoke-test") options.smoke_test = true;
        else if (args[i] == "--frames") {
            if (++i == args.size()) return error(i, "missing frame count");
            auto s = args[i];
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), options.frames);
            if (ec != std::errc{} || p != s.data() + s.size() || options.frames == 0)
                return error(i, "frame count must be a positive uint32");
        } else if(args[i]=="--renderer") {
            if(++i==args.size()||!valid_renderer_preference(args[i]))return error(i,"renderer must be auto, gpu or software");
            options.renderer=args[i];
        } else if (args[i] == "--report") {
            if (++i == args.size() || args[i].empty()) return error(i, "missing report path");
            options.report_path = args[i];
        } else if (args[i] == "--capture") {
            if (++i == args.size() || args[i].empty()) return error(i, "missing capture path");
            options.capture_path = args[i];
        } else if(args[i]=="--original-pack"||args[i]=="--remastered-pack"||args[i]=="--graphics-settings"||args[i]=="--save-directory") {
            auto option=args[i];if(++i==args.size()||args[i].empty())return error(i,"missing pack/settings path");
            if(option=="--original-pack")options.original_pack=args[i];else if(option=="--remastered-pack")options.remastered_pack=args[i];else if(option=="--graphics-settings")options.graphics_settings=args[i];else options.save_directory=args[i];
        } else if(args[i]=="--data"||args[i]=="--replay"||args[i]=="--checkpoint-out"||args[i]=="--opcode-trace"||args[i]=="--load-save") {
            auto option=args[i];if(++i==args.size()||args[i].empty())return error(i,"missing native path");
            if(option=="--data")options.data_path=args[i];else if(option=="--replay")options.replay_path=args[i];else if(option=="--checkpoint-out")options.checkpoint_out=args[i];else if(option=="--load-save")options.load_save=args[i];else options.opcode_trace_path=args[i];
        } else return error(i, "unknown option");
    }
    if((!options.replay_path.empty()||!options.checkpoint_out.empty()||!options.opcode_trace_path.empty()||!options.load_save.empty())&&options.data_path.empty())return error(0,"native replay/save paths require data");
    if(!options.data_path.empty()&&options.smoke_test)return error(0,"native launch and smoke mode are separate");
    if (!options.capture_path.empty() && !options.smoke_test)
        return error(0, "capture requires smoke mode");
    return options;
}
}
