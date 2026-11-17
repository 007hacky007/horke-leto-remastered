#include "image_io.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <cstdio>
namespace hl {
Result<void> write_png(const std::filesystem::path& path,uint32_t w,uint32_t h,uint32_t c,std::span<const uint8_t> pixels){
 if(!w||!h||w>16384||h>16384||c<1||c>4||uint64_t(w)*h*c!=pixels.size())return DataError{"",path.filename().string(),0,"invalid PNG dimensions/data"};
#ifdef _WIN32
 FILE* f=_wfopen(path.c_str(),L"wb");
#else
 FILE* f=std::fopen(path.c_str(),"wb");
#endif
 if(!f)return DataError{"",path.filename().string(),0,"cannot create PNG"};
 auto callback=[](void* context,void* data,int size){std::fwrite(data,1,size,static_cast<FILE*>(context));};
 int ok=stbi_write_png_to_func(callback,f,int(w),int(h),int(c),pixels.data(),int(w*c));bool failed=std::ferror(f);failed=std::fclose(f)!=0||failed;
 if(!ok||failed)return DataError{"",path.filename().string(),0,"PNG write failed"};return {};
}
Result<PngImage> read_png(const std::filesystem::path& path,uint32_t channels){
#ifdef _WIN32
 FILE* f=_wfopen(path.c_str(),L"rb");
#else
 FILE* f=std::fopen(path.c_str(),"rb");
#endif
 if(!f)return DataError{"",path.filename().string(),0,"cannot open PNG"};int w,h,c;
 if(channels<1||channels>4||!stbi_info_from_file(f,&w,&h,&c)||w<=0||h<=0||uint64_t(w)*h>16777216){std::fclose(f);return DataError{"",path.filename().string(),0,"invalid or excessive PNG dimensions"};}
 std::rewind(f);auto* p=stbi_load_from_file(f,&w,&h,&c,int(channels));std::fclose(f);if(!p)return DataError{"",path.filename().string(),0,"PNG decode failed"};PngImage out{uint32_t(w),uint32_t(h),channels,std::vector<uint8_t>(p,p+size_t(w)*h*channels)};stbi_image_free(p);return out;
}
}
