# ─── projectM (MilkDrop visualizer) ──────────────────────────────────────────
# Provides the targets libprojectM::projectM and libprojectM::playlist.
#   1. An installed projectM 4 (vcpkg, distro package, ...) is used if found.
#   2. Otherwise projectM is downloaded and built as a shared library (it is
#      LGPL-2.1; keeping it a separate DLL keeps licensing simple). On Windows
#      it needs GLEW, built here as glew32.dll: projectM doesn't call
#      glewInit() itself, so PulseAmp does, and both must share one GLEW.
# Sets PULSEAMP_HAVE_PROJECTM.

set(PULSEAMP_HAVE_PROJECTM OFF)

find_package(projectM4 CONFIG QUIET)
if(projectM4_FOUND AND TARGET libprojectM::projectM AND TARGET libprojectM::playlist)
    message(STATUS "projectM: using installed version ${projectM4_VERSION}")
    set(PULSEAMP_HAVE_PROJECTM ON)
    return()
endif()

if(NOT PULSEAMP_FETCH_PROJECTM)
    message(STATUS "projectM: not found, MilkDrop visualizer disabled "
                   "(install projectM 4 or set PULSEAMP_FETCH_PROJECTM=ON)")
    return()
endif()

include(FetchContent)

# GLEW (Windows only: projectM's OpenGL loader there)
if(WIN32)
    FetchContent_Declare(glew
        URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake-build)   # sources only; built below
    FetchContent_MakeAvailable(glew)
    add_library(pulseamp_glew SHARED ${glew_SOURCE_DIR}/src/glew.c)
    target_include_directories(pulseamp_glew PUBLIC ${glew_SOURCE_DIR}/include)
    target_compile_definitions(pulseamp_glew PRIVATE GLEW_BUILD)
    target_link_libraries(pulseamp_glew PUBLIC OpenGL::GL)
    set_target_properties(pulseamp_glew PROPERTIES OUTPUT_NAME glew32 PREFIX "")
    if(MINGW)
        # glew.c brings its own DLL entry point for GCC and uses no C runtime
        # (GLEW's own makefile links it the same way)
        target_link_options(pulseamp_glew PRIVATE -nostdlib)
        # ...so GCC must not turn loops into memset/memcpy calls
        target_compile_options(pulseamp_glew PRIVATE -fno-tree-loop-distribute-patterns -fno-builtin)
        target_link_libraries(pulseamp_glew PRIVATE kernel32)
    endif()
    add_library(GLEW::glew ALIAS pulseamp_glew)
    add_library(GLEW::GLEW   ALIAS pulseamp_glew)   # what CMake's FindGLEW inspects

    # projectM calls find_package(GLEW): answer it with the target above
    set(_shim ${CMAKE_BINARY_DIR}/glew-shim)
    file(WRITE ${_shim}/GLEWConfig.cmake
        "set(GLEW_FOUND TRUE)\nset(GLEW_VERSION 2.2.0)\n")
    set(GLEW_DIR ${_shim} CACHE PATH "GLEW config (PulseAmp shim)" FORCE)
endif()

# projectM itself
set(ENABLE_PLAYLIST ON CACHE BOOL "" FORCE)
set(ENABLE_SDL_UI OFF CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_GLM OFF CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_PROJECTM_EVAL OFF CACHE BOOL "" FORCE)
set(ENABLE_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(_pa_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ON)
FetchContent_Declare(projectm
    URL https://github.com/projectM-visualizer/projectm/releases/download/v4.1.7/libprojectM-4.1.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(projectm)
set(BUILD_SHARED_LIBS ${_pa_shared})

# MinGW: link the GCC runtime (libstdc++, libgcc, winpthread) into the projectM DLLs
if(MINGW)
    foreach(_t projectM projectM_playlist)
        if(TARGET ${_t})
            # (and never re-export symbols from those static runtime libs)
            target_link_options(${_t} PRIVATE -static -Wl,--exclude-libs,ALL)
        endif()
    endforeach()
endif()

message(STATUS "projectM: building 4.1.7 from source")
set(PULSEAMP_HAVE_PROJECTM ON)
