// ─── main.cpp ─────────────────────────────────────────────────────────────────
// PulseAmp — a modern C++17 media player
// Created by Farica Kimora — (c) 2026 Cursed Entertainment
// Entry point: SDL2 window + OpenGL 3.3 context, Dear ImGui, main loop.
// ─────────────────────────────────────────────────────────────────────────────
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

#include "player.h"
#include "audio_output.h"
#include "video_renderer.h"
#include "visualizer.h"
#include "theme_manager.h"
#include "converter.h"
#include "playlist.h"
#include "bpm_detector.h"
#include "spatial_audio.h"
#include "waveform.h"
#include "ui_manager.h"
#include "screenshot.h"
#include "fonts.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

// Design size: at this window size the UI scale is 1.0 (times the display DPI)
static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 780;

// UI scale for a window size: proportional to the window, never smaller than
// 75% of the display's DPI scale (so text stays readable in small windows)
static float uiScaleFor(int w, int h, float dpi_scale) {
    float s = std::min(w / (float)WIN_W, h / (float)WIN_H);
    s = std::clamp(s, 0.75f * dpi_scale, 4.f);
    return std::round(s * 20.f) / 20.f; // 5% steps: avoids rebuilding fonts constantly
}

int main(int argc, char* argv[]) {
    // ── Command line: media files, plus developer screenshot options ─────────
    std::vector<std::string> files;
    std::string shot_path, shot_show;
    int shot_w = WIN_W, shot_h = WIN_H, shot_frames = 30;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--screenshot" && i + 1 < argc) shot_path = argv[++i];
        else if (a == "--size"       && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &shot_w, &shot_h);
        else if (a == "--frames"     && i + 1 < argc) shot_frames = std::max(1, std::atoi(argv[++i]));
        else if (a == "--show"       && i + 1 < argc) shot_show = argv[++i];
        else files.push_back(a);
    }
    const bool shot = !shot_path.empty();

    // ── SDL init ──────────────────────────────────────────────────────────────
    // Real pixels on high-DPI Windows displays (we scale the UI ourselves)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Request OpenGL 3.3 core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Display scale (96 DPI = 1.0), used for the initial window size
    float dpi_scale = 1.f;
    float ddpi = 0.f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0.f)
        dpi_scale = std::clamp(ddpi / 96.f, 1.f, 4.f);
    int init_w = (int)(WIN_W * dpi_scale), init_h = (int)(WIN_H * dpi_scale);
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        init_w = std::min(init_w, usable.w * 9 / 10);
        init_h = std::min(init_h, usable.h * 9 / 10);
    }

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                       SDL_WINDOW_ALLOW_HIGHDPI;
    if (shot) { win_flags |= SDL_WINDOW_HIDDEN; init_w = shot_w; init_h = shot_h; }
    SDL_Window* window = SDL_CreateWindow(
        "PulseAmp " PULSEAMP_VERSION,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        init_w, init_h, win_flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // vsync
    SDL_SetWindowMinimumSize(window, 480, 320);

    // Enable drag-and-drop
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_EventState(SDL_DROPTEXT, SDL_ENABLE);   // links dragged from a browser
    SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
    SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);

    // ── ImGui init ────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; // disable imgui.ini

    int cur_w, cur_h;
    SDL_GetWindowSize(window, &cur_w, &cur_h);
    if (shot) { cur_w = shot_w; cur_h = shot_h; }
    float ui_scale = uiScaleFor(cur_w, cur_h, dpi_scale);
    Fonts::build(ui_scale);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ── Create subsystems ─────────────────────────────────────────────────────
    Player           player;
    AudioOutput      audio;
    VideoRenderer    video;
    Visualizer       viz;
    ThemeManager     themes;
    Converter        conv;
    Playlist         playlist;
    BPMDetector      bpm;
    SpatialAudio     spatial;
    WaveformGenerator waveform;

    spatial.init(44100);
    audio.setSpatial(&spatial);

    // Theme (the last one used is restored by ThemeManager)
    viz.setColor(themes.accentColor());

    UIManager ui(player, audio, video, viz, themes, conv, playlist, bpm, spatial, waveform);
    ui.setScale(ui_scale);

    // ── Command-line files ────────────────────────────────────────────────────
    for (auto& f : files)
        ui.addToPlaylist(f);
    if (playlist.size() > 0)
        ui.playEntry(0);

    // ── Screenshot mode: render into an offscreen framebuffer ────────────────
    GLuint shot_fbo = 0, shot_rbo = 0;
    auto glBindFramebuffer_ = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    if (shot) {
        auto glGenFramebuffers_        = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
        auto glGenRenderbuffers_       = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
        auto glBindRenderbuffer_       = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
        auto glRenderbufferStorage_    = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
        auto glFramebufferRenderbuffer_= (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
        glGenFramebuffers_(1, &shot_fbo);
        glGenRenderbuffers_(1, &shot_rbo);
        glBindRenderbuffer_(GL_RENDERBUFFER, shot_rbo);
        glRenderbufferStorage_(GL_RENDERBUFFER, GL_RGBA8, shot_w, shot_h);
        glBindFramebuffer_(GL_FRAMEBUFFER, shot_fbo);
        glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, shot_rbo);
        ui.devShow(shot_show);
    }
    int frame_no = 0;

    // ── Main loop ─────────────────────────────────────────────────────────────
    bool running = true;
    Uint64 freq  = SDL_GetPerformanceFrequency();
    Uint64 start = SDL_GetPerformanceCounter();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (!ui.handleEvent(e)) running = false;
        }
        if (ui.wantsQuit()) running = false;

        // Compute elapsed time
        Uint64 now  = SDL_GetPerformanceCounter();
        double time = (double)(now - start) / (double)freq;

        // Rescale the UI when the window size changed enough
        SDL_GetWindowSize(window, &cur_w, &cur_h);
        if (shot) { cur_w = shot_w; cur_h = shot_h; }
        float want = uiScaleFor(cur_w, cur_h, dpi_scale);
        const bool rescale = !ui.skinMode() && std::fabs(want - ui_scale) >= 0.049f;
        if (rescale || Fonts::consumeDirty()) {   // new size, or new CJK characters to show
            if (rescale) ui_scale = want;
            Fonts::build(ui_scale);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
            if (rescale) ui.setScale(ui_scale);
        }

        // Begin frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        if (shot) {
            w = shot_w; h = shot_h;
            ImGui::GetIO().DisplaySize = {(float)w, (float)h};
            ImGui::GetIO().DisplayFramebufferScale = {1.f, 1.f};
        }
        ImGui::NewFrame();

        // Render UI
        ui.render(w, h, time);

        // Draw
        ImGui::Render();
        ui.preRenderGL(ImGui::GetIO().DisplayFramebufferScale.x);
        int fb_w, fb_h;
        SDL_GL_GetDrawableSize(window, &fb_w, &fb_h);
        if (shot) { fb_w = shot_w; fb_h = shot_h; glBindFramebuffer_(GL_FRAMEBUFFER, shot_fbo); }
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.04f, 0.04f, 0.04f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (shot) {
            if (++frame_no >= shot_frames) {
                bool ok = saveFramebufferPNG(shot_path, shot_w, shot_h);
                std::printf("screenshot %s: %s\n", ok ? "saved" : "FAILED", shot_path.c_str());
                running = false;
            }
            SDL_Delay(16); // let playback / animations advance like real frames
            continue;
        }
        SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    // Save resume position before exit
    if (!player.getFilePath().empty())
        playlist.savePosition(player.getFilePath(), player.getCurrentTime());

    player.close();
    audio.close();
    waveform.cancel();
    conv.cancel();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
