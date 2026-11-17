# Run tools/distribution/fetch.py before configuring. CMake never downloads.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)  # the builder links SDL statically
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/vendor/SDL3-3.4.20 EXCLUDE_FROM_ALL)
add_library(hl-json INTERFACE)
target_include_directories(hl-json INTERFACE tools/vendor/json/single_include)
