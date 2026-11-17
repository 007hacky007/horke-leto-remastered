#include "launch_config.h"
#include "dialog_selection.h"
#include "platform/app.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <algorithm>
#include <optional>
#include <set>
namespace fs=std::filesystem;
using hl::delivery::utf8;
namespace {
using Selection=hl::delivery::DialogSelection;
void SDLCALL selected(void* context,const char* const* files,int){
 auto& result=*static_cast<Selection*>(context);
 if(!files)result.error=SDL_GetError();else if(files[0])result.file=files[0];
 result.done.store(true,std::memory_order_release);
}
fs::path choose(const char* title,bool folder){
 if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
 Selection result;auto properties=SDL_CreateProperties();
 SDL_SetStringProperty(properties,SDL_PROP_FILE_DIALOG_TITLE_STRING,title);
 if(folder)SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,selected,&result,properties);
 else SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE,selected,&result,properties);
 while(!result.done.load(std::memory_order_acquire)){SDL_Event event;SDL_WaitEventTimeout(&event,20);}
 SDL_DestroyProperties(properties);
 if(!result.error.empty())throw std::runtime_error(result.error);
 return fs::u8path(result.file);
}
bool folder_source(){
 SDL_MessageBoxButtonData buttons[]={{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"},{0,1,"BIN/CUE"},{0,2,"Extracted folder"}};
 SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,nullptr,"Import Horke Leto","Choose your game source. Original files stay unchanged.",3,buttons,nullptr};
 int choice=0;if(!SDL_ShowMessageBox(&box,&choice))throw std::runtime_error(SDL_GetError());
 if(!choice)throw std::runtime_error("cancelled");return choice==2;
}
void mute_audio(){
 if(!SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"SDL_AUDIO_DRIVER","dummy",true)||!SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
 const char* driver=SDL_GetCurrentAudioDriver();
 if(!driver||std::string_view(driver)!="dummy")throw std::runtime_error("Muted launch did not select the dummy audio driver");
 std::cerr<<"Launcher audio driver: "<<driver<<'\n';
}
int launch(std::vector<std::string> args){
 fs::path source,track2,track3,profile,packs;
 bool mute=false,import_only=false,dialogs=true;
 std::vector<std::string> engine;
 const std::set<std::string> engine_paths{"--data","--original-pack","--remastered-pack","--graphics-settings","--save-directory","--replay","--checkpoint-out","--opcode-trace","--report","--capture"};
 for(size_t i=0;i<args.size();++i){
  const auto& a=args[i];
  if(a=="--help"){std::cout<<"Horke Leto launcher\n--import-source BIN|CUE|FOLDER --track2 FILE --track3 FILE\n--packs FOLDER (contains original/ and remastered/)\n--profile FOLDER --data FOLDER --mute --import-only --no-dialogs\nEngine options such as --replay, --checkpoint-out and --renderer are forwarded.\n";return 0;}
  if(a=="--mute"){mute=true;continue;}if(a=="--import-only"){import_only=true;continue;}if(a=="--no-dialogs"){dialogs=false;continue;}
  if(a=="--import-source"||a=="--track2"||a=="--track3"||a=="--profile"||a=="--packs"){
   if(++i==args.size())throw std::runtime_error("Missing launcher option value");
   auto value=fs::absolute(fs::u8path(args[i]));
   if(a=="--import-source")source=value;else if(a=="--track2")track2=value;else if(a=="--track3")track3=value;else if(a=="--profile")profile=value;else packs=value;
  }else {
   engine.push_back(a);
   if(engine_paths.contains(a)){if(++i==args.size())throw std::runtime_error("Missing engine option value");engine.push_back(utf8(fs::absolute(fs::u8path(args[i]))));}
  }
 }
 if(profile.empty()){auto pref=SDL_GetPrefPath("hl-port","HorkeLeto");if(!pref)throw std::runtime_error(SDL_GetError());profile=fs::u8path(pref);SDL_free(pref);}
 const char* base=SDL_GetBasePath();if(!base)throw std::runtime_error(SDL_GetError());fs::path resources=fs::u8path(base);
#ifdef _WIN32
 resources/="Resources";
#endif
 fs::create_directories(profile);
 hl::delivery::Paths paths{resources/"data",resources/"packs"};
 auto config=fs::exists(resources/"launch.json")?resources/"launch.json":profile/"launch.json";
 if(fs::exists(config)){auto loaded=hl::delivery::load_paths(config);if(!loaded.has_value())throw std::runtime_error(loaded.error().reason);paths=loaded.value();}
 if(!packs.empty())paths.packs=packs;
 if(std::find(engine.begin(),engine.end(),"--data")==engine.end()&&std::find(engine.begin(),engine.end(),"--smoke-test")==engine.end()){engine.push_back("--data");engine.push_back(utf8(paths.data));}
 std::vector<std::string_view> views;for(const auto& a:engine)views.push_back(a);
 auto parsed=hl::parse_app_options(views);if(!parsed.has_value())throw std::runtime_error(parsed.error().reason);
 auto options=parsed.value();if(!options.data_path.empty())paths.data=fs::u8path(options.data_path);
 if(options.smoke_test){if(mute)mute_audio();return hl::run_app(options);}
 if(source.empty()&&!fs::is_directory(paths.data)){
  if(!dialogs)throw std::runtime_error("No game data. Use --import-source BIN|CUE|FOLDER and --packs FOLDER");
  source=choose("Select your BIN/CUE or HL folder",folder_source());if(source.empty())return 0;
 }
 if(!fs::is_directory(paths.packs)){
  if(!dialogs)throw std::runtime_error("No art packs. Use --packs FOLDER");
  paths.packs=choose("Select packs folder containing original and remastered",true);if(paths.packs.empty())return 0;
 }
 // The pinned encoder uses a resource-relative path, independent of the caller's working directory.
 fs::current_path(resources);
 if(!source.empty()){
  auto request=hl::delivery::import_source(source);if(!request.has_value())throw std::runtime_error(request.error().reason);
  auto req=request.value();
  if(!req.directory.empty()&&(!track2.empty()||!track3.empty()||!fs::exists(req.directory/"audio/track2.flac")||!fs::exists(req.directory/"audio/track3.flac"))){
   if(track2.empty()&&dialogs)track2=choose("Select track2.flac (44100 Hz stereo)",false);
   if(track3.empty()&&dialogs)track3=choose("Select track3.flac (44100 Hz stereo)",false);
   if(track2.empty()||track3.empty()){if(dialogs)return 0;throw std::runtime_error("Folder import requires both track2.flac and track3.flac");}
   req.cd_tracks={track2,track3};
  }
  auto installed=hl::delivery::install_data(req,profile,paths.packs);if(!installed.has_value())throw std::runtime_error(installed.error().reason);paths=installed.value();
 }else {
  auto valid=hl::delivery::validate_paths(paths);if(!valid.has_value())throw std::runtime_error(valid.error().reason);
 }
 if(import_only){SDL_Quit();return 0;}
 options.data_path=utf8(paths.data);options.original_pack=utf8(paths.packs/"original");options.remastered_pack=utf8(paths.packs/"remastered");
 if(options.save_directory.empty())options.save_directory=utf8(profile);
 if(mute)mute_audio();
 return hl::run_app(options);
}
int entry(std::vector<std::string> args){
 bool dialogs=std::find(args.begin(),args.end(),"--no-dialogs")==args.end();
 try {return launch(std::move(args));}
 catch(const std::exception& e){
  if(std::string_view(e.what())=="cancelled"){SDL_Quit();return 0;}
  std::cerr<<e.what()<<'\n';
  if(dialogs)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Horke Leto",e.what(),nullptr);
  SDL_Quit();return 1;
 }
}
}
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 int count=0;auto wide=CommandLineToArgvW(GetCommandLineW(),&count);if(!wide)return 1;
 std::vector<std::string> args;
 for(int i=1;i<count;++i){int size=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);std::string value(size,'\0');WideCharToMultiByte(CP_UTF8,0,wide[i],-1,value.data(),size,nullptr,nullptr);value.pop_back();args.push_back(std::move(value));}
 LocalFree(wide);return entry(std::move(args));
}
#else
int main(int argc,char** argv){return entry({argv+1,argv+argc});}
#endif
