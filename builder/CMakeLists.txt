# Native builder: one statically linked executable (SDL3 static, stb, json). It carries the
# payload (game template, importer, upscaler, FLAC encoder) as embedded data.
set(HB_ROOT ${CMAKE_SOURCE_DIR})
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pins.json HB_PINS)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pins.cpp
     CONTENT "namespace hb { extern const char* const EMBEDDED_PINS; const char* const EMBEDDED_PINS = R\"PINS(@HB_PINS@)PINS\"; }\n" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pins.json)
add_library(hb-core STATIC util.cpp image.cpp zip.cpp crypto.cpp disc.cpp stb.cpp original.cpp remastered.cpp
            pipeline.cpp gameicon.cpp ${CMAKE_CURRENT_BINARY_DIR}/pins.cpp ${HB_ROOT}/src/data/hash.cpp)
target_include_directories(hb-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${HB_ROOT}/src ${HB_ROOT}/tools/vendor/stb)
target_link_libraries(hb-core PUBLIC hl-json SDL3::SDL3-static)
target_compile_options(hb-core PRIVATE -O2)
if(APPLE)
  target_sources(hb-core PRIVATE net_mac.mm)
  set_source_files_properties(net_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(hb-core PUBLIC "-framework Foundation")
elseif(WIN32)
  target_sources(hb-core PRIVATE net_win.cpp)
  target_link_libraries(hb-core PUBLIC winhttp -static)
endif()
add_executable(hb-selftest selftest.cpp)
target_link_libraries(hb-selftest PRIVATE hb-core)
add_executable(hb-tests tests.cpp)
target_link_libraries(hb-tests PRIVATE hb-core)
add_test(NAME builder COMMAND hb-tests)
file(GLOB HB_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png)
set(HB_FONTS ${HB_ROOT}/tools/vendor/dejavu-fonts/ttf/DejaVuSans.ttf ${HB_ROOT}/tools/vendor/dejavu-fonts/ttf/DejaVuSans-Bold.ttf
             ${HB_ROOT}/tools/vendor/dejavu-fonts/ttf/DejaVuSansMono.ttf)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp
  COMMAND ${CMAKE_COMMAND} "-DFILES=${HB_ASSETS};${HB_FONTS}" -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  DEPENDS ${HB_ASSETS} ${HB_FONTS} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake VERBATIM)
add_executable(horke-leto-builder WIN32 MACOSX_BUNDLE main.cpp ui.cpp preflight.cpp ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp)
target_link_libraries(horke-leto-builder PRIVATE hb-core)
set_target_properties(horke-leto-builder PROPERTIES OUTPUT_NAME "Horke Leto Builder"
  MACOSX_BUNDLE_BUNDLE_NAME "Horké léto Builder" MACOSX_BUNDLE_GUI_IDENTIFIER "org.horkeleto.builder"
  MACOSX_BUNDLE_ICON_FILE builder-icon.icns MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0 MACOSX_BUNDLE_BUNDLE_VERSION 1)
if(APPLE)
  set(HB_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/art/builder-icon.icns)
  target_sources(horke-leto-builder PRIVATE ${HB_ICNS})
  set_source_files_properties(${HB_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_link_options(horke-leto-builder PRIVATE -Wl,-dead_strip)
elseif(WIN32)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/builder.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/art/builder-icon.ico\"\n")
  target_sources(horke-leto-builder PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/builder.rc)
  target_link_options(horke-leto-builder PRIVATE -static)
endif()
