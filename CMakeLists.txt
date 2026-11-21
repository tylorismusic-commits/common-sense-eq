cmake_minimum_required(VERSION 3.16)
project(common-sense-eq VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)
enable_testing()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(LILV QUIET IMPORTED_TARGET lilv-0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
# The editor host is X11 today.  macOS builds the portable targets (DSP,
# panel preview, tests) until the Cocoa view lands, so X11 stays optional.
option(CSE_NO_X11 "Skip the X11 editor targets (portable subset)" OFF)
if(APPLE OR CSE_NO_X11)
  set(CSE_X11 OFF)
else()
  pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
  set(CSE_X11 ON)
endif()

set(CSE_SOURCES
  src/ui/editor.cpp
  src/ui/panel.cpp)

function(cse_common target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/dsp"
    "${CMAKE_SOURCE_DIR}/src/cse")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -fvisibility=hidden)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# LV2 DSP + inline display (Harrison / Ardour)
# ---------------------------------------------------------------------------
add_library(common-sense-eq MODULE src/common_sense_eq.cpp)
cse_common(common-sense-eq)
target_link_libraries(common-sense-eq PRIVATE PkgConfig::LV2)
set_target_properties(common-sense-eq PROPERTIES
  PREFIX ""
  OUTPUT_NAME "common-sense-eq"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2")

set(CSE_BUNDLE_FILES
  "${CMAKE_SOURCE_DIR}/lv2/manifest.ttl"
  "${CMAKE_SOURCE_DIR}/lv2/common-sense-eq.ttl")

add_custom_target(common_sense_eq_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CSE_BUNDLE_FILES}
          "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2/"
  DEPENDS ${CSE_BUNDLE_FILES})
add_dependencies(common_sense_eq_bundle common-sense-eq)

# ---------------------------------------------------------------------------
# X11 / Cairo editor, exposed as an LV2 UI so the CLAP build can reuse it
# ---------------------------------------------------------------------------
if(CSE_X11)
add_library(common_sense_eq_ui MODULE ${CSE_SOURCES})
cse_common(common_sense_eq_ui)
target_compile_definitions(common_sense_eq_ui PRIVATE LV2_SYMBOL_EXPORT=)
target_link_libraries(common_sense_eq_ui PRIVATE
  PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2)
set_target_properties(common_sense_eq_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "CommonSenseEQUI"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2")

add_custom_target(common_sense_eq_ui_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:common_sense_eq_ui>"
          "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2/"
  DEPENDS common_sense_eq_ui)
endif()  # CSE_X11

add_executable(common_sense_eq_preset_test test/test_presets.cpp src/ui/panel.cpp)
cse_common(common_sense_eq_preset_test)
target_link_libraries(common_sense_eq_preset_test PRIVATE
  PkgConfig::CAIRO)
add_test(NAME common_sense_eq_presets COMMAND common_sense_eq_preset_test)

if(CSE_X11)
add_executable(common_sense_eq_ui_preview EXCLUDE_FROM_ALL ${CSE_SOURCES})
cse_common(common_sense_eq_ui_preview)
target_compile_definitions(common_sense_eq_ui_preview PRIVATE CSE_UI_PREVIEW)
target_link_libraries(common_sense_eq_ui_preview PRIVATE
  PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2)
endif()

# Full panel render with no window system: the macOS CI build uses this to
# prove panel.cpp compiles and draws with cairo alone.
add_executable(common_sense_eq_panel_preview tools/panel_preview.cpp src/ui/panel.cpp)
cse_common(common_sense_eq_panel_preview)
target_link_libraries(common_sense_eq_panel_preview PRIVATE PkgConfig::CAIRO)

add_executable(common_sense_eq_inline_preview EXCLUDE_FROM_ALL
  tools/inline_preview.cpp)
cse_common(common_sense_eq_inline_preview)
target_link_libraries(common_sense_eq_inline_preview PRIVATE PkgConfig::CAIRO)

# ---------------------------------------------------------------------------
# CLAP build
# ---------------------------------------------------------------------------
if(APPLE)
# macOS: the same CLAP, with the panel hosted in a Cocoa NSView.
enable_language(OBJCXX)
add_library(common_sense_eq_clap MODULE
  src/clap/common_sense_eq_clap.cpp
  src/ui/panel.cpp
  src/ui/cocoa_view.mm)
cse_common(common_sense_eq_clap)
target_compile_definitions(common_sense_eq_clap PRIVATE CSE_MACOS_COCOA=1)
target_compile_options(common_sense_eq_clap PRIVATE -fobjc-arc)
target_include_directories(common_sense_eq_clap PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/clap/include"
  ${CAIRO_INCLUDE_DIRS})
target_link_libraries(common_sense_eq_clap PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE
  "-framework Cocoa" "-framework CoreGraphics" "-framework QuartzCore"
  Threads::Threads)
set_target_properties(common_sense_eq_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "CommonSenseEQ"
  SUFFIX ".dylib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/macos")

set(CSE_CLAP_BUNDLE "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.clap")
add_custom_command(TARGET common_sense_eq_clap POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CSE_CLAP_BUNDLE}/Contents/MacOS"
  COMMAND ${CMAKE_COMMAND} -E copy
          "$<TARGET_FILE:common_sense_eq_clap>"
          "${CSE_CLAP_BUNDLE}/Contents/MacOS/CommonSenseEQ"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist"
          "${CSE_CLAP_BUNDLE}/Contents/Info.plist")
add_custom_target(common_sense_eq_clap_bundle ALL
  DEPENDS common_sense_eq_clap)
elseif(CSE_X11)
add_library(common_sense_eq_clap MODULE src/clap/common_sense_eq_clap.cpp)
cse_common(common_sense_eq_clap)
target_include_directories(common_sense_eq_clap PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/clap/include"
  ${CAIRO_INCLUDE_DIRS})
target_link_libraries(common_sense_eq_clap PRIVATE
  PkgConfig::LV2 ${CMAKE_DL_LIBS} Threads::Threads)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/clap")
set_target_properties(common_sense_eq_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "CommonSenseEQ"
  SUFFIX ".clap"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap")

add_custom_target(common_sense_eq_clap_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:common_sense_eq_ui>"
          "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.resources/CommonSenseEQUI.so"
  DEPENDS common_sense_eq_clap common_sense_eq_ui)
endif()  # CSE_X11

install(TARGETS common-sense-eq
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/common-sense-eq.lv2")
install(FILES lv2/manifest.ttl lv2/common-sense-eq.ttl
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/common-sense-eq.lv2")
if(CSE_X11)
  install(TARGETS common_sense_eq_ui
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2/common-sense-eq.lv2")
endif()
if(APPLE)
  install(DIRECTORY "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.clap"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/clap")
elseif(CSE_X11)
  install(TARGETS common_sense_eq_clap
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/clap")
  install(DIRECTORY "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.resources"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/clap")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(common_sense_eq_dsp_test test/test_dsp.cpp)
cse_common(common_sense_eq_dsp_test)
add_test(NAME common_sense_eq_dsp COMMAND common_sense_eq_dsp_test)

if(LILV_FOUND)
  add_executable(common_sense_eq_host_test test/host_test.cpp)
  cse_common(common_sense_eq_host_test)
  target_link_libraries(common_sense_eq_host_test PRIVATE PkgConfig::LILV
    PkgConfig::LV2)
  add_dependencies(common_sense_eq_host_test common_sense_eq_bundle)
  if(CSE_X11)
    add_dependencies(common_sense_eq_host_test common_sense_eq_ui_bundle)
  endif()
  add_test(NAME common_sense_eq_host
    COMMAND common_sense_eq_host_test "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2")
endif()

find_library(CSE_XTST_LIBRARY Xtst)
find_program(CSE_XVFB_RUN xvfb-run)
if(CSE_X11 AND CSE_XTST_LIBRARY AND CSE_XVFB_RUN)
  add_executable(common_sense_eq_ui_host_test test/ui_host_test.cpp)
  cse_common(common_sense_eq_ui_host_test)
  target_link_libraries(common_sense_eq_ui_host_test PRIVATE
    PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2 ${CSE_XTST_LIBRARY}
    ${CMAKE_DL_LIBS})
  add_dependencies(common_sense_eq_ui_host_test common_sense_eq_ui_bundle)
  add_test(NAME common_sense_eq_ui_host
    COMMAND "${CSE_XVFB_RUN}" -a -s "-screen 0 1400x900x24"
      "$<TARGET_FILE:common_sense_eq_ui_host_test>"
      "${CMAKE_BINARY_DIR}/lv2/common-sense-eq.lv2/CommonSenseEQUI.so"
      "${CMAKE_BINARY_DIR}/common-sense-eq-ui-live.png")
endif()

if(CSE_X11)
add_executable(common_sense_eq_clap_host_test test/clap_host_test.cpp)
cse_common(common_sense_eq_clap_host_test)
target_include_directories(common_sense_eq_clap_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(common_sense_eq_clap_host_test PRIVATE
  ${CMAKE_DL_LIBS} Threads::Threads)
if(CSE_XTST_LIBRARY)
  target_compile_definitions(common_sense_eq_clap_host_test PRIVATE
    CSE_HAVE_XTEST)
  target_link_libraries(common_sense_eq_clap_host_test PRIVATE
    PkgConfig::X11 PkgConfig::CAIRO ${CSE_XTST_LIBRARY})
endif()
add_dependencies(common_sense_eq_clap_host_test common_sense_eq_clap_bundle)
add_test(NAME common_sense_eq_clap_host
  COMMAND common_sense_eq_clap_host_test
    "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.clap"
    "com.tylorismusic.commonsense")
if(CSE_XTST_LIBRARY AND CSE_XVFB_RUN)
  add_test(NAME common_sense_eq_clap_gui
    COMMAND "${CSE_XVFB_RUN}" -a -s "-screen 0 1400x900x24"
      "$<TARGET_FILE:common_sense_eq_clap_host_test>"
      "${CMAKE_BINARY_DIR}/clap/CommonSenseEQ.clap"
      "com.tylorismusic.commonsense" "--gui")
endif()
endif()  # CSE_X11
