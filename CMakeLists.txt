cmake_minimum_required(VERSION 3.20)
project(PulseAmp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    enable_language(RC)   # version info resource (credits in Properties > Details)
endif()

# ─── Options ──────────────────────────────────────────────────────────────────
option(PULSEAMP_BUNDLE_FFMPEG "Copy FFmpeg and SDL2 DLLs next to executable" OFF)
option(PULSEAMP_WITH_PROJECTM "MilkDrop visualizer (projectM)" ON)
option(PULSEAMP_FETCH_PROJECTM "Download and build projectM if it isn't installed" ON)

# Put the exe and every DLL we build (projectM) in one folder, so it runs from there
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})   # Linux .so files (projectM)

# ─── FetchContent ─────────────────────────────────────────────────────────────
include(FetchContent)

# Download the release archive (much faster than cloning the full git history)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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

# miniz: reads zipped skins (.wsz / .paskin)
FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake-build   # amalgamated sources only; built below
)
FetchContent_MakeAvailable(miniz)
add_library(pulseamp_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(pulseamp_miniz PUBLIC ${miniz_SOURCE_DIR})

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
# On Linux: apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIB avformat)
find_library(AVCODEC_LIB  avcodec)
find_library(AVUTIL_LIB   avutil)
find_library(SWSCALE_LIB  swscale)
find_library(SWRESAMPLE_LIB swresample)

foreach(LIB AVFORMAT_INCLUDE_DIR AVFORMAT_LIB AVCODEC_LIB AVUTIL_LIB SWSCALE_LIB SWRESAMPLE_LIB)
    if(NOT ${LIB})
        message(FATAL_ERROR "FFmpeg not found (${LIB}). See README.md for build instructions.")
    endif()
endforeach()

if(PULSEAMP_WITH_PROJECTM)
    include(cmake/projectm.cmake)
endif()

set(FFMPEG_LIBRARIES
    ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB}
    ${SWSCALE_LIB}  ${SWRESAMPLE_LIB}
)

# ─── PulseAmp executable ─────────────────────────────────────────────────────
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
    src/paths.cpp
    src/screenshot.cpp
    src/online.cpp
    src/fonts.cpp
    src/milkdrop.cpp
    src/json.cpp
    src/skin.cpp
    src/skin_window.cpp
    src/ui_skin.cpp
)

add_executable(PulseAmp ${SOURCES})

if(WIN32)
    configure_file(src/pulseamp.rc.in ${CMAKE_CURRENT_BINARY_DIR}/pulseamp.rc @ONLY)
    target_sources(PulseAmp PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/pulseamp.rc)
endif()
if(MSVC)
    target_compile_options(PulseAmp PRIVATE /utf-8)  # sources contain UTF-8 text (©, ─)
endif()

target_compile_definitions(PulseAmp PRIVATE PULSEAMP_VERSION="${PROJECT_VERSION}")
if(PULSEAMP_HAVE_PROJECTM)
    target_compile_definitions(PulseAmp PRIVATE PULSEAMP_HAVE_PROJECTM=1)
    target_link_libraries(PulseAmp PRIVATE libprojectM::projectM libprojectM::playlist)
    if(TARGET pulseamp_glew)   # Windows: PulseAmp calls glewInit() for projectM
        target_link_libraries(PulseAmp PRIVATE pulseamp_glew)
        target_compile_definitions(PulseAmp PRIVATE PULSEAMP_USE_GLEW=1)
    endif()
endif()

target_include_directories(PulseAmp PRIVATE
    src
    ${AVFORMAT_INCLUDE_DIR}
)

# SDL2main must come before SDL2 on the link line (MinGW resolves symbols in order)
if(TARGET SDL2::SDL2main)
    target_link_libraries(PulseAmp PRIVATE SDL2::SDL2main)
endif()

target_link_libraries(PulseAmp PRIVATE
    imgui
    pulseamp_miniz
    ${FFMPEG_LIBRARIES}
    OpenGL::GL
)

if(TARGET SDL2::SDL2)
    target_link_libraries(PulseAmp PRIVATE SDL2::SDL2)
else()
    target_include_directories(PulseAmp PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(PulseAmp PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32)
    # comdlg32/shell32/ole32: native Open File / Open Folder dialogs
    target_link_libraries(PulseAmp PRIVATE ws2_32 bcrypt comdlg32 shell32 ole32)
    # MinGW: link the GCC runtime statically so the exe doesn't need
    # libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll next to it
    if(MINGW)
        # (FFmpeg/SDL2 are linked by full path to their import libs, so they stay DLLs)
        target_link_options(PulseAmp PRIVATE -static)
    endif()
    # Hide console window in Release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(PulseAmp PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
elseif(UNIX AND NOT APPLE)
    target_link_libraries(PulseAmp PRIVATE pthread dl)
    # Shaped skin windows on X11 (libxext-dev); without it skins stay rectangular
    find_package(X11)
    if(X11_FOUND AND X11_Xext_FOUND AND X11_Xshape_INCLUDE_PATH)
        target_link_libraries(PulseAmp PRIVATE X11::X11 X11::Xext)
        target_compile_definitions(PulseAmp PRIVATE PULSEAMP_HAVE_XSHAPE=1)
    endif()
endif()

# Ship the bundled skins next to the exe
add_custom_command(TARGET PulseAmp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/skins $<TARGET_FILE_DIR:PulseAmp>/skins)

# ─── Bundle FFmpeg + SDL2 DLLs (Windows) ──────────────────────────────────────
set(FFMPEG_BIN_DIR "" CACHE PATH "Directory containing FFmpeg .dll files")
if(PULSEAMP_BUNDLE_FFMPEG AND WIN32)
    if(TARGET SDL2::SDL2)
        add_custom_command(TARGET PulseAmp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:PulseAmp>)
    endif()
    if(FFMPEG_BIN_DIR)
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
        foreach(DLL ${FFMPEG_DLLS})
            add_custom_command(TARGET PulseAmp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:PulseAmp>)
        endforeach()
    endif()
endif()

# ─── Install rules ────────────────────────────────────────────────────────────
install(TARGETS PulseAmp RUNTIME DESTINATION bin)
