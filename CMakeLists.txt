cmake_minimum_required(VERSION 3.20)
project(NovPlayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Options ──────────────────────────────────────────────────────────────────
option(NOV_BUNDLE_FFMPEG "Copy FFmpeg DLLs next to executable" OFF)
option(NOV_BUILD_INSTALLER "Generate NSIS installer target" OFF)

# ─── FetchContent ─────────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# ─── Find SDL2 ────────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
if(TARGET SDL2::SDL2)
    target_link_libraries(imgui PUBLIC SDL2::SDL2)
else()
    target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES})
endif()

# ─── Find OpenGL ──────────────────────────────────────────────────────────────
find_package(OpenGL REQUIRED)
target_link_libraries(imgui PUBLIC OpenGL::GL)

# ─── Find FFmpeg ──────────────────────────────────────────────────────────────
# On Windows with vcpkg: -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# On Linux: apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIB avformat)
find_library(AVCODEC_LIB  avcodec)
find_library(AVUTIL_LIB   avutil)
find_library(SWSCALE_LIB  swscale)
find_library(SWRESAMPLE_LIB swresample)
find_library(AVFILTER_LIB avfilter)

if(NOT AVFORMAT_LIB)
    message(FATAL_ERROR "FFmpeg not found. See BUILD.md for instructions.")
endif()

set(FFMPEG_LIBRARIES
    ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB}
    ${SWSCALE_LIB}  ${SWRESAMPLE_LIB} ${AVFILTER_LIB}
)

# ─── NovPlayer executable ─────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/player.cpp
    src/audio_output.cpp
    src/video_renderer.cpp
    src/visualizer.cpp
    src/theme_manager.cpp
    src/ui_manager.cpp
    src/converter.cpp
    src/playlist.cpp
    src/bpm_detector.cpp
    src/spatial_audio.cpp
    src/waveform.cpp
)

add_executable(NovPlayer ${SOURCES})

target_include_directories(NovPlayer PRIVATE
    src
    ${AVFORMAT_INCLUDE_DIR}
)

target_link_libraries(NovPlayer PRIVATE
    imgui
    ${FFMPEG_LIBRARIES}
    OpenGL::GL
)

if(TARGET SDL2::SDL2)
    target_link_libraries(NovPlayer PRIVATE SDL2::SDL2 SDL2::SDL2main)
else()
    target_include_directories(NovPlayer PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(NovPlayer PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(NovPlayer PRIVATE ws2_32 bcrypt)
    # Hide console window in Release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(NovPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
elseif(UNIX AND NOT APPLE)
    target_link_libraries(NovPlayer PRIVATE pthread dl)
endif()

# ─── Bundle FFmpeg DLLs (Windows) ─────────────────────────────────────────────
if(NOV_BUNDLE_FFMPEG AND WIN32)
    set(FFMPEG_BIN_DIR "" CACHE PATH "Directory containing FFmpeg .dll files")
    if(FFMPEG_BIN_DIR)
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
        foreach(DLL ${FFMPEG_DLLS})
            add_custom_command(TARGET NovPlayer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:NovPlayer>)
        endforeach()
    endif()
endif()

# ─── NSIS Installer ───────────────────────────────────────────────────────────
if(NOV_BUILD_INSTALLER AND WIN32)
    find_program(MAKENSIS makensis)
    if(MAKENSIS)
        add_custom_target(installer
            COMMAND ${MAKENSIS} /DPRODUCT_VERSION=${PROJECT_VERSION}
                    /DAPP_EXE=$<TARGET_FILE:NovPlayer>
                    ${CMAKE_SOURCE_DIR}/installer/setup.nsi
            DEPENDS NovPlayer
            COMMENT "Building NSIS installer…")
    endif()
endif()

# ─── Install rules ────────────────────────────────────────────────────────────
install(TARGETS NovPlayer RUNTIME DESTINATION bin)
