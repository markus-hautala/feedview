cmake_minimum_required(VERSION 3.21)

project(feedview VERSION 1.0.0 LANGUAGES C CXX)

set(APP_NAME "FeedView")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FEEDVIEW_BUILD_TEST_SENDER "Build the test-pattern NDI sender tool" ON)
option(FEEDVIEW_BUILD_TESTS "Build the display and web-remote tests (run with ctest)" ON)

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

# ---- Web remote: HTTP server, QR codes for the on-screen address, JPEG for previews.
# These are header/source-only; SOURCE_SUBDIR points at a folder without a CMakeLists so
# nothing of their own build (tests, optional OpenSSL...) is pulled in.
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.59.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_Declare(qrcodegen
  GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
  GIT_TAG v1.8.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  SOURCE_SUBDIR no-cmake)

FetchContent_MakeAvailable(SDL3 imgui httplib qrcodegen stb)

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

# ---- Embedded files: UI font (Roboto Medium, Apache 2.0, ships with Dear ImGui) and the
# web remote page. Regenerated whenever the source file changes.
function(embed_file input symbol output_var)
  set(out ${CMAKE_BINARY_DIR}/generated/${symbol}.cpp)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${out} -DSYMBOL=${symbol}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${input} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding ${input}"
    VERBATIM)
  set(${output_var} ${out} PARENT_SCOPE)
endfunction()
embed_file(${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf kUiFontData FONT_CPP)
embed_file(${CMAKE_CURRENT_SOURCE_DIR}/web/index.html kWebPage PAGE_CPP)

# ---- NDI runtime loader (headers only; the runtime is loaded at run time)
add_library(ndi_runtime STATIC src/ndi_runtime.cpp)
target_include_directories(ndi_runtime PUBLIC src third_party/ndi/include)
if(UNIX AND NOT APPLE)
  target_link_libraries(ndi_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

# ---- Display handling logic (no SDL dependency, unit-tested)
add_library(display_manager STATIC src/display_manager.cpp)
target_include_directories(display_manager PUBLIC src)

# ---- Live production: when the controls show, and what FeedView controls in the OS
# (system volume/output, notifications, staying on top). No SDL dependency, tested.
add_library(live_control STATIC src/overlay_activity.cpp src/take_fade.cpp src/os_control.cpp)
target_include_directories(live_control PUBLIC src)
if(WIN32)
  target_link_libraries(live_control PUBLIC ole32 advapi32 userenv shell32 dwmapi user32)
  target_compile_definitions(live_control PRIVATE _WIN32_WINNT=0x0A00)
endif()

# ---- Settings file (file access through SDL)
add_library(settings STATIC src/settings.cpp)
target_include_directories(settings PUBLIC src)
target_link_libraries(settings PUBLIC SDL3::SDL3)

# ---- Third-party single-file implementations (built without our warning flags)
add_library(third_party_impl STATIC src/third_party_impl.cpp ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(third_party_impl PUBLIC src ${stb_SOURCE_DIR} ${qrcodegen_SOURCE_DIR}/cpp)

# ---- Web remote server (no SDL dependency, unit-tested)
find_package(Threads REQUIRED)
add_library(remote_server STATIC src/remote_server.cpp src/remote_view.cpp src/net_info.cpp)
target_include_directories(remote_server PUBLIC src ${httplib_SOURCE_DIR})
target_link_libraries(remote_server PUBLIC ndi_runtime third_party_impl Threads::Threads)
if(WIN32)
  target_link_libraries(remote_server PUBLIC ws2_32 iphlpapi)
  target_compile_definitions(remote_server PUBLIC _WIN32_WINNT=0x0A00)  # Windows 10+
endif()

# ---- The app

add_executable(feedview WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/ndi_io.cpp
  ${FONT_CPP}
  ${PAGE_CPP})
target_link_libraries(feedview PRIVATE ndi_runtime display_manager live_control settings remote_server imgui SDL3::SDL3)
target_compile_definitions(feedview PRIVATE FEEDVIEW_VERSION="${PROJECT_VERSION}")
set_target_properties(feedview PROPERTIES OUTPUT_NAME ${APP_NAME})
if(MSVC)
  target_compile_options(feedview PRIVATE /utf-8)
endif()
target_link_libraries(feedview PRIVATE Threads::Threads)

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

if(FEEDVIEW_BUILD_TESTS)
  enable_testing()
  add_executable(display_manager_test tests/display_manager_test.cpp)
  target_link_libraries(display_manager_test PRIVATE display_manager)
  add_test(NAME display_manager COMMAND display_manager_test)
  add_executable(remote_server_test tests/remote_server_test.cpp)
  target_link_libraries(remote_server_test PRIVATE remote_server)
  add_test(NAME remote_server COMMAND remote_server_test)
  # Live-production rules (controls, PIN default, settings, remote links) - pure logic.
  add_executable(live_control_test tests/live_control_test.cpp)
  target_link_libraries(live_control_test PRIVATE live_control settings remote_server)
  add_test(NAME live_control COMMAND live_control_test)
  # Against the real OS of this computer: system audio, notifications, window stacking.
  # Changes are undone at once (volume +-1 and back, notifications restored).
  add_executable(os_control_test tests/os_control_test.cpp)
  target_link_libraries(os_control_test PRIVATE live_control)
  add_test(NAME os_control COMMAND os_control_test)
  set_tests_properties(os_control PROPERTIES LABELS desktop RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
  # End to end: starts FeedView and the test sender and checks each live-production
  # requirement through the web remote API, Windows and simulated mouse/keyboard input.
  # It takes over the screen for about a minute.
  if(WIN32 AND TARGET feedview_test_sender)
    add_executable(e2e_test tests/e2e_test.cpp)
    target_link_libraries(e2e_test PRIVATE live_control remote_server)
    add_test(NAME e2e COMMAND e2e_test $<TARGET_FILE:feedview> $<TARGET_FILE:feedview_test_sender>)
    set_tests_properties(e2e PROPERTIES LABELS "desktop;e2e" RUN_SERIAL TRUE TIMEOUT 300 SKIP_RETURN_CODE 77)
  endif()

  # Your requests (tests/REQUIREMENTS.md): every one has tests, and the ways to run them are
  # there. A static check with no dependencies, so it runs everywhere (CI too).
  add_test(NAME requirements_coverage
           COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/requirements_coverage.cmake)

  # The release package to share (scripts/make-release.ps1): only what users need, and the
  # FeedView in it runs with its bundled NDI runtime. Uses the existing build.
  if(WIN32)
    add_test(NAME release_package
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_package_test.ps1
                     -BuildDir ${CMAKE_BINARY_DIR})
    set_tests_properties(release_package PROPERTIES TIMEOUT 300)
  endif()

  # Tests written for Node.js (scenario results as TAP, which scripts/requirements-report reads).
  find_program(NODE_EXECUTABLE node)
  set(FEEDVIEW_TEST_ENV "FEEDVIEW_EXE=$<TARGET_FILE:feedview>;FEEDVIEW_SENDER=$<TARGET_FILE:feedview_test_sender>")

  # FeedView's web page in a real browser (headless Edge/Chrome) against the real FeedView:
  # every control on the page. Needs Node.js 22+ only (no packages); goes fullscreen briefly.
  if(NODE_EXECUTABLE AND TARGET feedview_test_sender)
    add_test(NAME web_remote COMMAND ${NODE_EXECUTABLE} --test --test-reporter=tap tests/web/web_remote.test.mjs
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(web_remote PROPERTIES LABELS desktop RUN_SERIAL TRUE TIMEOUT 300 ENVIRONMENT "${FEEDVIEW_TEST_ENV}")
  endif()

  # The Bitfocus Companion module (companion/feedview, Node.js). Its tests need Node.js and
  # `npm ci` in that folder (scripts/run-tests does it); without them they're left out.
  set(COMPANION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/companion/feedview)
  if(NODE_EXECUTABLE AND EXISTS ${COMPANION_DIR}/node_modules/@companion-module/base)
    # Logic, and the module as Companion runs it against a stand-in for FeedView's API.
    add_test(NAME companion_module
             COMMAND ${NODE_EXECUTABLE} --test --test-concurrency=1 --test-reporter=tap test/logic.test.js test/module.test.js
             WORKING_DIRECTORY ${COMPANION_DIR})
    # Every action against the real FeedView (goes fullscreen briefly).
    if(TARGET feedview_test_sender)
      add_test(NAME companion_feedview COMMAND ${NODE_EXECUTABLE} --test --test-reporter=tap test/feedview.test.js
               WORKING_DIRECTORY ${COMPANION_DIR})
      set_tests_properties(companion_feedview PROPERTIES LABELS desktop RUN_SERIAL TRUE TIMEOUT 300
        ENVIRONMENT "${FEEDVIEW_TEST_ENV}")
    endif()
  else()
    message(STATUS "Companion module tests left out: needs Node.js and `npm ci` in companion/feedview")
  endif()

  # Keep every test's whole output (ctest shortens passing tests' output by default), so the
  # request report sees every scenario.
  file(WRITE ${CMAKE_BINARY_DIR}/CTestCustom.cmake
       "set(CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE 1048576)\nset(CTEST_CUSTOM_MAXIMUM_FAILED_TEST_OUTPUT_SIZE 1048576)\n")
endif()

foreach(t feedview ndi_runtime display_manager display_manager_test remote_server remote_server_test
          live_control live_control_test os_control_test e2e_test settings feedview_test_sender)
  if(TARGET ${t})
    if(MSVC)
      target_compile_options(${t} PRIVATE /W3)
    else()
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
  endif()
endforeach()
