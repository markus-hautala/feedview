cmake_minimum_required(VERSION 3.21)

project(feedview VERSION 1.0.0 LANGUAGES C CXX)

set(APP_NAME "FeedView")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FEEDVIEW_BUILD_TEST_SENDER "Build the test-pattern NDI sender tool" ON)

# Windows: link the C runtime statically so users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- SDL3 (window, rendering, audio, input) - built statically into the app
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.16
  GIT_SHALLOW TRUE)

# ---- Dear ImGui (the overlay UI)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(SDL3 imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ---- Embed the UI font (Roboto Medium, Apache 2.0, shipped with Dear ImGui)
set(FONT_TTF ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf)
set(FONT_CPP ${CMAKE_BINARY_DIR}/generated/ui_font.cpp)
if(NOT EXISTS ${FONT_CPP} OR ${FONT_TTF} IS_NEWER_THAN ${FONT_CPP})
  file(READ ${FONT_TTF} FONT_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
  string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" FONT_BYTES "${FONT_BYTES}")
  file(WRITE ${FONT_CPP}
    "// Generated from Roboto-Medium.ttf - do not edit.\n"
    "extern const unsigned char kUiFontData[] = {\n${FONT_BYTES}\n};\n"
    "extern const unsigned int kUiFontDataSize = sizeof(kUiFontData);\n")
endif()

# ---- NDI runtime loader (headers only; the runtime is loaded at run time)
add_library(ndi_runtime STATIC src/ndi_runtime.cpp)
target_include_directories(ndi_runtime PUBLIC src third_party/ndi/include)
if(UNIX AND NOT APPLE)
  target_link_libraries(ndi_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

# ---- The app
add_executable(feedview WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/ndi_io.cpp
  src/settings.cpp
  ${FONT_CPP})
target_link_libraries(feedview PRIVATE ndi_runtime imgui SDL3::SDL3)
target_compile_definitions(feedview PRIVATE FEEDVIEW_VERSION="${PROJECT_VERSION}")
set_target_properties(feedview PROPERTIES OUTPUT_NAME ${APP_NAME})
if(MSVC)
  target_compile_options(feedview PRIVATE /utf-8)
endif()
if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(feedview PRIVATE Threads::Threads)
endif()

if(APPLE)
  set_target_properties(feedview PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME ${APP_NAME}
    MACOSX_BUNDLE_GUI_IDENTIFIER app.feedview.player
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in)
endif()

if(FEEDVIEW_BUILD_TEST_SENDER)
  add_executable(feedview_test_sender tools/test_sender.cpp)
  target_link_libraries(feedview_test_sender PRIVATE ndi_runtime)
  set_target_properties(feedview_test_sender PROPERTIES OUTPUT_NAME feedview-test-sender)
  if(MSVC)
    target_compile_options(feedview_test_sender PRIVATE /utf-8)
  endif()
endif()

foreach(t feedview ndi_runtime feedview_test_sender)
  if(TARGET ${t})
    if(MSVC)
      target_compile_options(${t} PRIVATE /W3)
    else()
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
  endif()
endforeach()
