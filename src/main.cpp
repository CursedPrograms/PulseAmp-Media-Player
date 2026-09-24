// ─── main.cpp ─────────────────────────────────────────────────────────────────
// NovPlayer — a modern C++17 media player
// Entry point: SDL2 window + OpenGL 3.3 context, Dear ImGui, main loop.
// ─────────────────────────────────────────────────────────────────────────────
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
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

// Initial window dimensions
static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 780;

int main(int argc, char* argv[]) {
    // ── SDL init ──────────────────────────────────────────────────────────────
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

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                       SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow(
        "NovPlayer 1.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, win_flags);
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

    // Enable drag-and-drop
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    // ── ImGui init ────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; // disable imgui.ini

    // Font: try to load a nice system font, fall back to built-in
#ifdef _WIN32
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",  15.f);
#elif __APPLE__
    io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/Supplemental/Arial.ttf", 15.f);
#else
    io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14.f);
#endif
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();

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

    // Apply default theme
    themes.applyTheme(Theme::NeonAmp);
    viz.setColor(themes.accentColor());

    UIManager ui(player, audio, video, viz, themes, conv, playlist, bpm, spatial, waveform);

    // ── Command-line files ────────────────────────────────────────────────────
    for (int i = 1; i < argc; ++i)
        ui.addToPlaylist(argv[i]);
    if (playlist.size() > 0)
        ui.playEntry(0);

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

        // Compute elapsed time
        Uint64 now  = SDL_GetPerformanceCounter();
        double time = (double)(now - start) / (double)freq;

        // Begin frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int w, h;
        SDL_GetWindowSize(window, &w, &h);

        // Render UI
        ui.render(w, h, time);

        // Draw
        ImGui::Render();
        glViewport(0, 0, w, h);
        glClearColor(0.04f, 0.04f, 0.04f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
