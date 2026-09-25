// ─── ui_manager.cpp ───────────────────────────────────────────────────────────
#include "ui_manager.h"
#include "fonts.h"
#include "paths.h"
#include <fstream>
#include <imgui.h>
#include <SDL2/SDL.h>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <cctype>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min/std::max usable (MSVC)
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

static const char* VIZ_NAMES[] = {
    "Spectrum Bars", "Oscilloscope", "Radial Spectrum",
    "BPM Pulse", "Particle Storm", "MilkDrop"
};

// ─────────────────────────────────────────────────────────────────────────────
UIManager::UIManager(Player& player, AudioOutput& audio, VideoRenderer& video,
                     Visualizer& viz, ThemeManager& themes, Converter& conv,
                     Playlist& playlist, BPMDetector& bpm, SpatialAudio& spatial,
                     WaveformGenerator& waveform)
    : player_(player), audio_(audio), video_(video), viz_(viz),
      themes_(themes), conv_(conv), playlist_(playlist),
      bpm_(bpm), spatial_(spatial), waveform_(waveform)
{
    viz_.setColor(themes_.accentColor());
    loadSettings();
}

UIManager::~UIManager() {
    saveSettings();
}

// ─── Settings: <appdata>/settings.ini (key=value) ────────────────────────────
void UIManager::loadSettings() {
    std::ifstream f(appDataPath("settings.ini"));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        double d = std::atof(v.c_str());
        if      (k == "skin")         skin_path_ = v;
        else if (k == "skin_zoom")    skin_zoom_ = std::clamp((float)d, 1.f, 4.f);
        else if (k == "classic")      restore_classic_ = v == "1";
        else if (k == "volume")       { volume_ = std::clamp((float)d, 0.f, 1.f); player_.setVolume(volume_); }
        else if (k == "viz_mode")     { int m = (int)d; if (m >= 0 && m < vizModeCount()) viz_.setMode((Visualizer::Mode)m); }
        else if (k == "online_video") online_video_ = v == "1";
        else if (k == "md_shuffle")   md_shuffle_ = v == "1";
        else if (k == "md_duration")  md_duration_ = std::clamp((float)d, 5.f, 120.f);
    }
}

void UIManager::saveSettings() const {
    std::ofstream f(appDataPath("settings.ini"));
    if (!f) return;
    f << "skin=" << skin_path_ << "\n"
      << "skin_zoom=" << skin_zoom_ << "\n"
      << "classic=" << (skin_mode_ ? 1 : 0) << "\n"
      << "volume=" << volume_ << "\n"
      << "viz_mode=" << (int)viz_.getMode() << "\n"
      << "online_video=" << (online_video_ ? 1 : 0) << "\n"
      << "md_shuffle=" << (md_shuffle_ ? 1 : 0) << "\n"
      << "md_duration=" << md_duration_ << "\n";
}

// ─── Event handling ───────────────────────────────────────────────────────────
bool UIManager::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_QUIT) return false;

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_SPACE: {
                auto s = player_.getState();
                if (s == PlayerState::Playing) player_.pause();
                else                           player_.play();
                break;
            }
            case SDLK_LEFT:  player_.seek(player_.getCurrentTime() - 5.0);  break;
            case SDLK_RIGHT: player_.seek(player_.getCurrentTime() + 5.0);  break;
            case SDLK_UP:    volume_ = std::min(1.f, volume_ + 0.05f);
                             player_.setVolume(volume_); break;
            case SDLK_DOWN:  volume_ = std::max(0.f, volume_ - 0.05f);
                             player_.setVolume(volume_); break;
            case SDLK_m:
                if (e.key.keysym.mod & KMOD_CTRL) toggleSkinMode();
                else player_.setMuted(!player_.isMuted());
                break;
            case SDLK_f:     toggleFullscreen(); break;
            case SDLK_n:     { auto* ne = playlist_.next();
                               if (ne) playEntry(playlist_.getIndex()); break; }
            case SDLK_p:     { auto* pr = playlist_.prev();
                               if (pr) playEntry(playlist_.getIndex()); break; }
            case SDLK_o:     openFile(); break;
            case SDLK_LEFTBRACKET:  if (viz_.getMode() == Visualizer::Mode::MilkDrop) milkdrop_.previous(); break;
            case SDLK_RIGHTBRACKET: if (viz_.getMode() == Visualizer::Mode::MilkDrop) milkdrop_.next();     break;
            case SDLK_TAB:   show_sidebar_ = !show_sidebar_; break;
            case SDLK_v:     show_viz_ = !show_viz_; break;
            case SDLK_ESCAPE:
                if (fullscreen_) { toggleFullscreen(); break; }
                if (skin_mode_)  { toggleSkinMode();  break; }
                return false;
            default: break;
        }
    }

    // Drag and drop from outside: files / folders from Explorer, links from a browser.
    // SDL sends DROPBEGIN, one DROPFILE/DROPTEXT per item, then DROPCOMPLETE.
    if (e.type == SDL_DROPBEGIN) beginExternalDrop();
    if (e.type == SDL_DROPFILE || e.type == SDL_DROPTEXT) {
        if (drop_start_ < 0) beginExternalDrop();   // (a drop without DROPBEGIN)
        std::string item = e.drop.file;
        SDL_free(e.drop.file);
        while (!item.empty() && std::isspace((unsigned char)item.back())) item.pop_back();
        if (e.type == SDL_DROPFILE || isUrl(item)) addToPlaylist(item);
    }
    if (e.type == SDL_DROPCOMPLETE) finishExternalDrop();

    return true;
}

// ─── Main render ──────────────────────────────────────────────────────────────
void UIManager::render(int win_w, int win_h, double time) {
    pollBackgroundWork();

    // Keep the audio output in sync with the player (applied instantly in the callback)
    audio_.pause(player_.getState() != PlayerState::Playing);
    audio_.setGain(player_.isMuted() ? 0.f : player_.getVolume());
    audio_.setSpatialWidth(spatial_width_);

    // Handle end-of-file on the main thread: advance the playlist
    if (player_.pollEnded()) {
        // Finished: forget the resume point so it starts from the top next time
        playlist_.savePosition(player_.getFilePath(), 0.0);
        const PlaylistEntry* ne = auto_advance_ ? playlist_.next() : nullptr;
        if (ne) playEntry(playlist_.getIndex());
        else    player_.pause();
    }

    // Pick up a finished waveform from the generator thread
    {
        std::lock_guard lk(waveform_mu_);
        if (waveform_pending_ready_) {
            waveform_peaks_ = std::move(waveform_pending_);
            waveform_pending_ready_ = false;
            waveform_ready_ = true;
        }
    }

    // Update BPM
    if (player_.getState() == PlayerState::Playing) {
        constexpr int BPM_FEED = 1024;
        static std::vector<float> bpm_mono(BPM_FEED);
        player_.getAudioBuffer().peekNext(bpm_mono.data(), BPM_FEED);
        // Mix to mono
        for (int i = 0; i < BPM_FEED/2; ++i)
            bpm_mono[i] = (bpm_mono[i*2] + bpm_mono[i*2+1]) * 0.5f;
        float bpm = bpm_.feed(bpm_mono.data(), BPM_FEED/2, player_.getSampleRate());
        if (bpm > 0) displayed_bpm_ = bpm;
    }

    // Mood color update
    if (mood_color_ && time - mood_timer_ > 0.5) {
        mood_timer_ = time;
        ImU32 mc = viz_.moodColor();
        if ((mc & 0xFFFFFF) > 0x101010)
            viz_.setColor(mc);
    }

    // Feed MilkDrop (silence when not playing, so it settles down)
    milkdrop_w_ = milkdrop_h_ = 0.f;
    if (viz_.getMode() == Visualizer::Mode::MilkDrop && milkdrop_.ok())
        milkdrop_.feed(player_.getAudioBuffer(), ImGui::GetIO().DeltaTime, player_.getSampleRate(),
                       player_.getState() == PlayerState::Playing);

    // Feed visualizer
    if (player_.getState() == PlayerState::Playing ||
        player_.getState() == PlayerState::Paused)
        viz_.feed(player_.getAudioBuffer());

    // Info overlay fade
    overlay_timer_ += ImGui::GetIO().DeltaTime;
    if (overlay_timer_ > 3.0f)
        overlay_alpha_ = std::max(0.f, overlay_alpha_ - ImGui::GetIO().DeltaTime * 0.5f);

    // ── Classic mode: the skin is the whole window ─────────────────────────
    if (restore_classic_) { restore_classic_ = false; if (!skin_mode_) toggleSkinMode(); }
    if (skin_mode_ && skin_) {
        drawSkinMode(win_w, win_h, time);
        return;
    }

    // ── Fullscreen: hide menu/transport/cursor after a few idle seconds ──────
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    if (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f || ImGui::IsAnyMouseDown())
        last_activity_ = now;
    const bool chrome = !fullscreen_ || now - last_activity_ < 3.0 ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || show_theme_editor_;
    if (chrome == cursor_hidden_) {
        cursor_hidden_ = !chrome;
        SDL_ShowCursor(chrome ? SDL_ENABLE : SDL_DISABLE);
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    // Windowed: menu / content / transport stacked, sidebar on the right.
    // Fullscreen: content fills the screen, menu + transport float on top.
    const float MENUBAR_H   = chrome ? ImGui::GetFrameHeight() : 0.f;
    const float TRANSPORT_H = S(72.f);
    const bool  sidebar     = show_sidebar_ && !fullscreen_;
    const float SIDEBAR_W   = sidebar ? S(340.f) : 0.f;
    const float CONTENT_Y   = fullscreen_ ? 0.f : MENUBAR_H;
    const float CONTENT_W   = win_w - SIDEBAR_W;
    const float CONTENT_H   = fullscreen_ ? (float)win_h : win_h - MENUBAR_H - TRANSPORT_H;
    menubar_h_ = MENUBAR_H;
    chrome_visible_ = chrome;

    // In fullscreen show the video if there is one, otherwise the visualizer
    const bool  video_on = show_video_ && (!fullscreen_ || player_.hasVideo());
    const bool  viz_on   = show_viz_   && (!fullscreen_ || !player_.hasVideo());
    const float VIZ_H    = !viz_on ? 0.f : !video_on ? CONTENT_H
        : std::max(S(80.f), CONTENT_H * viz_height_pct_);
    const float VIDEO_H  = video_on ? CONTENT_H - VIZ_H : 0.f;

    float cy = CONTENT_Y;
    if (video_on && VIDEO_H > 0) drawVideoPanel(0, cy, CONTENT_W, VIDEO_H);
    cy += VIDEO_H;
    if (viz_on && VIZ_H > 0) drawVizPanel(0, cy, CONTENT_W, VIZ_H, time);

    if (chrome) {
        drawMenuBar();
        drawTransportBar(0, win_h - TRANSPORT_H, (float)win_w, TRANSPORT_H);
    }
    if (sidebar)
        drawSidebar(CONTENT_W, MENUBAR_H, SIDEBAR_W, CONTENT_H, time);

    // Info overlay
    if (info_overlay_ && overlay_alpha_ > 0.01f)
        drawInfoOverlay(time);

    // Double-click on the video / visualizer toggles fullscreen
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImVec2 m = io.MousePos;
        bool in_content = m.x >= 0 && m.x < CONTENT_W &&
                          m.y >= CONTENT_Y && m.y < CONTENT_Y + CONTENT_H;
        bool on_chrome  = chrome && (m.y < MENUBAR_H || m.y >= win_h - TRANSPORT_H);
        bool over_popup_window = show_about_ || show_theme_editor_;
        if (in_content && !on_chrome && !over_popup_window) toggleFullscreen();
    }

    if (show_theme_editor_) drawThemeEditor();

    if (show_about_) drawAboutWindow();
}

// ─── About / credits ──────────────────────────────────────────────────────────
void UIManager::drawAboutWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::Begin("About PulseAmp", &show_about_,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(themes_.accentColor()),
                       "PulseAmp %s", PULSEAMP_VERSION);
    ImGui::TextUnformatted("A modern C++17 media player");
    ImGui::Separator();
    ImGui::TextUnformatted("Created by Farica Kimora");
    ImGui::TextUnformatted("© 2026 Cursed Entertainment"); // UTF-8 ©
    ImGui::Separator();
    ImGui::TextDisabled("Built with FFmpeg (LGPL), SDL2 and Dear ImGui");
    ImGui::Spacing();
    if (ImGui::Button("Close", {-1, 0})) show_about_ = false;
    ImGui::End();
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────
void UIManager::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open File\t(O)"))   openFile();
        if (ImGui::MenuItem("Open Folder"))      openFolder();
        if (ImGui::MenuItem("Open URL..."))      { show_sidebar_ = true; sidebar_tab_ = 1; focus_link_ = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit\t(Esc)")) quit_requested_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Playback")) {
        bool playing = player_.getState() == PlayerState::Playing;
        if (ImGui::MenuItem(playing ? "Pause\t(Space)" : "Play\t(Space)")) {
            if (playing) player_.pause(); else player_.play();
        }
        if (ImGui::MenuItem("Stop"))      player_.stop();
        if (ImGui::MenuItem("Next\t(N)")) { auto* ne=playlist_.next(); if(ne)playEntry(playlist_.getIndex()); }
        if (ImGui::MenuItem("Prev\t(P)")) { auto* pr=playlist_.prev(); if(pr)playEntry(playlist_.getIndex()); }
        ImGui::Separator();
        bool muted = player_.isMuted();
        if (ImGui::MenuItem("Mute\t(M)", nullptr, muted)) player_.setMuted(!muted);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Sidebar\t(Tab)", nullptr, show_sidebar_)) show_sidebar_ = !show_sidebar_;
        if (ImGui::MenuItem("Visualizer\t(V)", nullptr, show_viz_))    show_viz_ = !show_viz_;
        if (ImGui::MenuItem("Info Overlay",  nullptr, info_overlay_))  { info_overlay_ = !info_overlay_; overlay_alpha_=1.f; overlay_timer_=0; }
        if (ImGui::MenuItem("Fullscreen\t(F / double-click)", nullptr, fullscreen_)) toggleFullscreen();
        if (ImGui::MenuItem("Classic Mode (skins)\tCtrl+M")) toggleSkinMode();
        drawSkinList();
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            drawThemeList(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Theme Editor...")) openThemeEditor();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Visualizer Mode")) {
            for (int i = 0; i < vizModeCount(); ++i) {
                bool sel = ((int)viz_.getMode() == i);
                if (ImGui::MenuItem(VIZ_NAMES[i], nullptr, sel))
                    viz_.setMode((Visualizer::Mode)i);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("YouTube / SoundCloud")) { show_sidebar_ = true; sidebar_tab_ = 1; }
        if (ImGui::MenuItem("Converter")) { show_sidebar_ = true; sidebar_tab_ = 2; }
        if (ImGui::MenuItem("Settings"))  { show_sidebar_ = true; sidebar_tab_ = 3; }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About PulseAmp")) show_about_ = true;
        ImGui::EndMenu();
    }

    // Right-side BPM display
    if (displayed_bpm_ > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "  %.0f BPM ", displayed_bpm_);
        float bw = ImGui::CalcTextSize(buf).x + 10.f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - bw + ImGui::GetCursorPosX());
        ImGui::TextDisabled("%s", buf);
    }

    ImGui::EndMainMenuBar();
}

// ─── Video panel ──────────────────────────────────────────────────────────────
void UIManager::drawVideoPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowSize({w, h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.02f,0.02f,0.02f,1.f});
    ImGui::Begin("##video", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

    // Poll video frame
    auto vf = player_.pollVideoFrame();
    if (vf) video_.uploadFrame(*vf);

    if (video_.isReady() && player_.hasVideo()) {
        float vw = (float)video_.getWidth();
        float vh = (float)video_.getHeight();
        float ar = vw / std::max(1.f, vh);
        float dw = w, dh = h;
        if (dw / ar > dh) dw = dh * ar;
        else              dh = dw / ar;
        float ox = (w - dw) * 0.5f, oy = (h - dh) * 0.5f;
        ImGui::SetCursorPos({ox, oy});
        ImGui::Image(video_.getTextureID(), {dw, dh});
    } else {
        // No-video placeholder
        std::string m;
        const PlaylistEntry* cur_e = playlist_.current();
        if (open_job_.valid())
            m = "Loading " + open_job_title_ + " ...";
        else if (!online_status_.empty())
            m = online_status_;
        else if (player_.hasAudio() && cur_e)
            m = cur_e->title;
        else
            m = "Drop a file or a link here to play";
        const char* msg = m.c_str();
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(
            {x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f},
            ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ─── Visualizer panel ─────────────────────────────────────────────────────────
void UIManager::drawVizPanel(float x, float y, float w, float h, double time) {
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowSize({w, h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    ImGui::Begin("##viz", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (viz_.getMode() == Visualizer::Mode::MilkDrop) {
        drawMilkDrop(x, y, w, h);
    } else {
        viz_.render(dl, {x, y}, {w, h}, displayed_bpm_, time);
    }

    // Cycle mode button (top-right; hidden with the other controls in fullscreen)
    if (chrome_visible_) {
        ImGui::SetCursorPos({w - S(130.f), S(4.f)});
        ImGui::PushStyleColor(ImGuiCol_Button, {0.f,0.f,0.f,0.4f});
        if (ImGui::Button(VIZ_NAMES[(int)viz_.getMode()], {S(125.f), 0.f})) {
            int next = ((int)viz_.getMode() + 1) % vizModeCount();
            viz_.setMode((Visualizer::Mode)next);
        }
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ─── Waveform seek bar (unique feature) ───────────────────────────────────────
void UIManager::drawWaveformSeekBar(float x, float y, float w, float h) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 bg  = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32 acc = themes_.accentColor();
    ImU32 acc_dim = IM_COL32(
        ((acc>>IM_COL32_R_SHIFT)&0xFF)/3,
        ((acc>>IM_COL32_G_SHIFT)&0xFF)/3,
        ((acc>>IM_COL32_B_SHIFT)&0xFF)/3, 200);

    dl->AddRectFilled({x,y},{x+w,y+h}, bg, S(3.f));

    double dur = player_.getDuration();
    double cur = player_.getCurrentTime();

    if (!waveform_peaks_.empty()) {
        int N = (int)waveform_peaks_.size();
        for (int i = 0; i < N; ++i) {
            float px  = x + (float)i/N * w;
            float pw  = w / N - 0.5f;
            float ph  = waveform_peaks_[i] * h * 0.9f;
            float py  = y + (h - ph) * 0.5f;
            bool past = dur > 0 && (double)i/N < cur/dur;
            dl->AddRectFilled({px,py},{px+pw,py+ph}, past ? acc : acc_dim);
        }
    } else {
        // Simple progress fill
        if (dur > 0) {
            float frac = (float)(cur/dur);
            dl->AddRectFilled({x,y},{x+w*frac,y+h}, acc_dim, S(3.f));
        }
    }

    // Playhead
    if (dur > 0) {
        float fx = x + (float)(cur/dur)*w;
        dl->AddLine({fx,y},{fx,y+h}, IM_COL32(255,255,255,200), S(2.f));
    }

    // Interactive seek
    ImGui::SetCursorPos({x - ImGui::GetWindowPos().x, y - ImGui::GetWindowPos().y});
    ImGui::InvisibleButton("##seekbar", {w,h});
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        float mx = ImGui::GetIO().MousePos.x;
        float frac = std::clamp((mx - x) / w, 0.f, 1.f);
        player_.seek(frac * dur);
        overlay_timer_ = 0; overlay_alpha_ = 1.f;
    }
    if (ImGui::IsItemHovered() && dur > 0) {
        float mx = ImGui::GetIO().MousePos.x;
        float frac = std::clamp((mx - x) / w, 0.f, 1.f);
        ImGui::SetTooltip("%s", formatTime(frac * dur).c_str());
    }
}

// ─── Transport bar ────────────────────────────────────────────────────────────
void UIManager::drawTransportBar(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowSize({w, h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(8.f), S(6.f)});
    ImVec4 tbg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    tbg.w = fullscreen_ ? 0.85f : 1.f; // floats over the video in fullscreen
    ImGui::PushStyleColor(ImGuiCol_WindowBg, tbg);
    ImGui::Begin("##transport", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav);

    // Row 1: Seek bar
    float bar_y = y + S(6.f);
    float bar_h = S(18.f);
    drawWaveformSeekBar(x + S(8.f), bar_y, w - S(16.f), bar_h);

    ImGui::SetCursorPosY(bar_h + S(10.f));

    // Row 2: Controls
    float btn_w = S(36.f), btn_h = S(22.f);
    bool playing = player_.getState() == PlayerState::Playing;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));

    if (ImGui::Button("|<##prev",{btn_w,btn_h})) {
        auto* pr = playlist_.prev(); if(pr) playEntry(playlist_.getIndex());
    }
    ImGui::SameLine(0,S(4.f));
    if (ImGui::Button(playing ? "||##pause" : "> ##play", {btn_w+S(10.f),btn_h})) {
        if (playing) player_.pause(); else player_.play();
    }
    ImGui::SameLine(0,S(4.f));
    if (ImGui::Button("[]##stop",{btn_w,btn_h})) player_.stop();
    ImGui::SameLine(0,S(4.f));
    if (ImGui::Button(">|##next",{btn_w,btn_h})) {
        auto* ne = playlist_.next(); if(ne) playEntry(playlist_.getIndex());
    }
    ImGui::PopStyleColor();

    // Shuffle / Repeat
    ImGui::SameLine(0,S(12.f));
    bool shuf = playlist_.getShuffle();
    if (ImGui::Button(shuf ? "[S]" : " S ", {S(28.f),btn_h})) playlist_.setShuffle(!shuf);
    ImGui::SameLine(0,S(4.f));
    RepeatMode rep = playlist_.getRepeat();
    const char* rep_lbl = rep==RepeatMode::All ? "[R]" : rep==RepeatMode::One ? "[1]" : " R ";
    if (ImGui::Button(rep_lbl, {S(28.f),btn_h})) {
        playlist_.setRepeat(rep==RepeatMode::None ? RepeatMode::All :
                            rep==RepeatMode::All  ? RepeatMode::One : RepeatMode::None);
    }

    // Time display
    ImGui::SameLine(0,S(16.f));
    double cur = player_.getCurrentTime(), dur = player_.getDuration();
    ImGui::Text("%s / %s", formatTime(cur).c_str(), formatTime(dur).c_str());

    // Volume
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90.f));
    float vol_pct = volume_ * 100.f;
    if (ImGui::SliderFloat("##vol", &vol_pct, 0.f, 100.f, "Vol %.0f%%", ImGuiSliderFlags_None)) {
        volume_ = std::clamp(vol_pct / 100.f, 0.f, 1.f);
        player_.setVolume(volume_);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) volume_ = 1.f, player_.setVolume(1.f);

    // File name
    const PlaylistEntry* pe = playlist_.current();
    if (pe) {
        ImGui::SameLine(0,S(16.f));
        ImGui::TextDisabled("%s", pe->title.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ─── Sidebar ──────────────────────────────────────────────────────────────────
void UIManager::drawSidebar(float x, float y, float w, float h, double time) {
    (void)time;
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowSize({w, h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(6.f),S(6.f)});
    ImGui::Begin("##sidebar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Tabs
    const char* tabs[] = {"Playlist","Online","Convert","Settings","Chapters"};
    constexpr int N_TABS = 5;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {S(4.f),S(3.f)});
    const float tab_w = (ImGui::GetContentRegionAvail().x - (N_TABS - 1) * S(2.f)) / N_TABS;
    for (int i = 0; i < N_TABS; ++i) {
        if (i > 0) ImGui::SameLine(0, S(2.f));
        bool active = (sidebar_tab_ == i);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tabs[i], {tab_w, S(22.f)})) sidebar_tab_ = i;
        if (i == 0 && ImGui::GetDragDropPayload() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            sidebar_tab_ = 0;   // dragging a search result: open the playlist to drop it
        if (active) ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::Separator();

    switch (sidebar_tab_) {
        case 0: drawPlaylistTab();  break;
        case 1: drawOnlineTab();    break;
        case 2: drawConverterTab(); break;
        case 3: drawSettingsTab();  break;
        case 4: drawChaptersTab();  break;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ─── Playlist tab ─────────────────────────────────────────────────────────────
void UIManager::drawPlaylistTab() {
    auto& entries = playlist_.entries();
    int cur = playlist_.getIndex();

    if (ImGui::Button("+ Add File", {-1,S(22.f)})) openFile();

    ImGui::BeginChild("##pl_list", {-1, -S(28.f)}, false);
    pl_list_min_ = ImGui::GetWindowPos();
    pl_list_max_ = {pl_list_min_.x + ImGui::GetWindowWidth(), pl_list_min_.y + ImGui::GetWindowHeight()};
    pl_row_top_.clear();
    int move_from = -1, move_to = -1, add_result = -1, add_at = -1;

    // Accept a playlist row (reorder) or an Online search result (insert) dropped at row `at`
    auto acceptDrop = [&](int at) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PA_PL_ROW"))
            { move_from = *(const int*)pl->Data; move_to = at; }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PA_ONLINE"))
            { add_result = *(const int*)pl->Data; add_at = at; }
        ImGui::EndDragDropTarget();
    };

    for (int i = 0; i < (int)entries.size(); ++i) {
        const auto& e = entries[i];
        bool sel = (i == cur);
        ImGui::PushID(i);
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        Fonts::noteText(e.title);
        char label[256];
        snprintf(label, sizeof(label), "%s##pl%d", e.title.c_str(), i);
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) playEntry(i);
        }
        if (sel) ImGui::PopStyleColor();
        pl_row_top_.push_back(ImGui::GetItemRectMin().y);
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("PA_PL_ROW", &i, sizeof(int));
            ImGui::TextUnformatted(e.title.c_str());
            ImGui::EndDragDropSource();
        }
        acceptDrop(i);
        if (ImGui::BeginPopupContextItem("##plctx")) {
            if (ImGui::MenuItem("Play"))       playEntry(i);
            if (ImGui::MenuItem("Remove"))     playlist_.removeAt(i);
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", e.path.c_str());
        ImGui::PopID();
    }
    // Empty space below the rows: drop here to append
    float rest = ImGui::GetContentRegionAvail().y;
    if (rest > 1.f) {
        ImGui::InvisibleButton("##pl_end", {-1, rest});
        acceptDrop((int)entries.size());
        if (entries.empty()) {
            const char* hint = "Drop files, folders or links here";
            ImVec2 ts = ImGui::CalcTextSize(hint);
            ImVec2 c = {(pl_list_min_.x + pl_list_max_.x - ts.x) * 0.5f, (pl_list_min_.y + pl_list_max_.y - ts.y) * 0.5f};
            ImGui::GetWindowDrawList()->AddText(c, ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        }
    }
    ImGui::EndChild();

    if (move_from >= 0) {
        // Dropping below the last row means "to the end"
        playlist_.move(move_from, std::min(move_to, playlist_.size() - 1));
    }
    if (add_result >= 0 && add_result < (int)online_results_.size()) {
        const auto& r = online_results_[add_result];
        int first = playlist_.size();
        playlist_.addFile(r.url, r.title, r.duration);
        insertEntries(first, add_at);
    }

    if (ImGui::Button("Clear", {-1,S(20.f)})) playlist_.clear();
}

// ─── Converter tab ────────────────────────────────────────────────────────────
void UIManager::drawConverterTab() {
    ConvertProgress prog;
    { std::lock_guard lk(conv_mu_); prog = conv_prog_; }

    auto fmts = Converter::availableFormats();

    ImGui::TextDisabled("Source:");
    ImGui::SameLine();
    const PlaylistEntry* pe = playlist_.current();
    std::string src = conv_source_path_.empty()
        ? (pe ? pe->path : "(none)") : conv_source_path_;
    ImGui::TextWrapped("%s", fs::path(src).filename().string().c_str());
    if (ImGui::Button("Browse Source", {-1,S(20.f)})) {
        // Use current playlist item or open dialog
        if (pe) conv_source_path_ = pe->path;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Output format:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##fmt", fmts[conv_fmt_idx_].c_str())) {
        for (int i = 0; i < (int)fmts.size(); ++i)
            if (ImGui::Selectable(fmts[i].c_str(), conv_fmt_idx_==i))
                conv_fmt_idx_ = i;
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Output path:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##outpath", conv_output_buf_, sizeof(conv_output_buf_));

    ImGui::Spacing();
    ImGui::TextDisabled("Audio kbps:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(100.f));
    ImGui::InputInt("##abr", &conv_audio_kbps_);
    ImGui::TextDisabled("Video kbps:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(100.f));
    ImGui::InputInt("##vbr", &conv_vid_kbps_);
    ImGui::Checkbox("Strip video (audio only)", &conv_strip_vid_);
    ImGui::Checkbox("Strip audio (video only)", &conv_strip_aud_);

    ImGui::Spacing();
    ImGui::Separator();

    bool running = conv_.isRunning();
    if (running) {
        ImGui::ProgressBar((float)prog.progress, {-1,S(18.f)});
        ImGui::TextDisabled("%.0f%%  %s / %s",
            prog.progress*100,
            formatTime(prog.current_time).c_str(),
            formatTime(prog.duration).c_str());
        if (ImGui::Button("Cancel", {-1,S(22.f)})) conv_.cancel();
    } else {
        if (prog.done && !prog.error)
            ImGui::TextColored({0.2f,1.f,0.4f,1.f}, "Done!");
        if (prog.error)
            ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "Error: %s", prog.error_msg.c_str());

        if (ImGui::Button("Convert", {-1,S(22.f)})) {
            std::string s = conv_source_path_.empty() && pe ? pe->path : conv_source_path_;
            std::string o = conv_output_buf_[0] ? conv_output_buf_ :
                (fs::path(s).parent_path() /
                 (fs::path(s).stem().string() + "_conv." + fmts[conv_fmt_idx_])).string();
            ConvertJob j;
            j.input_path   = s;
            j.output_path  = o;
            j.output_format= fmts[conv_fmt_idx_];
            j.audio_bitrate= conv_audio_kbps_ * 1000;
            j.video_bitrate= conv_vid_kbps_   * 1000;
            j.strip_video  = conv_strip_vid_;
            j.strip_audio  = conv_strip_aud_;
            { std::lock_guard lk(conv_mu_); conv_prog_ = {}; }
            conv_.start(j, [this](ConvertProgress p){
                std::lock_guard lk(conv_mu_);  // called from the converter thread
                conv_prog_ = std::move(p);
            });
        }
    }
}

// ─── Settings tab ─────────────────────────────────────────────────────────────
void UIManager::drawSettingsTab() {
    ImGui::SeparatorText("Playback");
    ImGui::Checkbox("Smart Resume",     &smart_resume_);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remembers playback position for every file");
    ImGui::Checkbox("Auto-advance",     &auto_advance_);
    ImGui::Spacing();

    ImGui::SeparatorText("Visualizer");
    ImGui::TextDisabled("Mode:");
    ImGui::SetNextItemWidth(-1);
    int mode = (int)viz_.getMode();
    if (ImGui::Combo("##vizmode", &mode, VIZ_NAMES, vizModeCount()))
        viz_.setMode((Visualizer::Mode)mode);

    ImGui::TextDisabled("Viz Height:");
    ImGui::SetNextItemWidth(-1);
    float vizh_pct = viz_height_pct_ * 100.f;
    if (ImGui::SliderFloat("##vizh", &vizh_pct, 10.f, 50.f, "%.0f%%"))
        viz_height_pct_ = vizh_pct / 100.f;

    // Accent colour picker
    ImGui::TextDisabled("Accent Color:");
    ImU32 ac = viz_.getColor();
    float col[4] = {
        ((ac>>IM_COL32_R_SHIFT)&0xFF)/255.f,
        ((ac>>IM_COL32_G_SHIFT)&0xFF)/255.f,
        ((ac>>IM_COL32_B_SHIFT)&0xFF)/255.f, 1.f};
    if (ImGui::ColorEdit3("##acc", col, ImGuiColorEditFlags_NoInputs))
        viz_.setColor(IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),255));

    ImGui::Checkbox("Mood Adaptive Color", &mood_color_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Visualizer accent color shifts with the frequency mood of the music");

    if (MilkDrop::compiledIn()) {
        ImGui::SeparatorText("MilkDrop");
        if (!milkdrop_.ok()) {
            if (ImGui::Button("Start MilkDrop", {-1, 0}))
                viz_.setMode(Visualizer::Mode::MilkDrop);
        } else {
            ImGui::TextDisabled("%d presets", milkdrop_.presetCount());
            if (ImGui::Checkbox("Shuffle presets", &md_shuffle_)) milkdrop_.setShuffle(md_shuffle_);
            if (ImGui::Checkbox("Lock current preset", &md_locked_)) milkdrop_.setLocked(md_locked_);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##mddur", &md_duration_, 5.f, 120.f, "Change every %.0f s"))
                milkdrop_.setDuration(md_duration_);
            if (ImGui::Button("Rescan presets", {-1, 0})) milkdrop_.rescanPresets();
            for (auto& d : milkdrop_.presetDirs()) {
                Fonts::noteText(d);
                ImGui::TextDisabled("%s", d.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put .milk preset files (and folders) here");
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Spatial Audio");
    bool spa = spatial_.isEnabled();
    if (ImGui::Checkbox("Stereo Widener (headphones)", &spa)) spatial_.setEnabled(spa);
    if (spa) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##spw", &spatial_width_, 0.f, 2.f, "Width %.2f");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Skins (Classic mode)");
    if (ImGui::Button("Classic Mode  (Ctrl+M)", {-1, 0})) toggleSkinMode();
    if (ImGui::Button("Load skin file...", {-1, 0})) { openSkinFile(); if (skin_ && !skin_mode_) toggleSkinMode(); }
    if (!skin_error_.empty()) ImGui::TextWrapped("%s", skin_error_.c_str());
    ImGui::TextDisabled("Winamp .wsz skins: skins.webamp.org");

    ImGui::SeparatorText("Theme");
    drawThemeList(false);
    if (ImGui::Button("Theme Editor...", {-1, 0})) openThemeEditor();
}

// ─── Themes ───────────────────────────────────────────────────────────────────
// List of themes as menu items (menu bar) or radio buttons (settings tab)
void UIManager::drawThemeList(bool as_menu) {
    const auto& list = themes_.themes();
    for (int i = 0; i < (int)list.size(); ++i) {
        bool sel = (i == themes_.currentIndex());
        bool clicked = as_menu ? ImGui::MenuItem(list[i].name.c_str(), nullptr, sel)
                               : ImGui::RadioButton(list[i].name.c_str(), sel);
        if (clicked) {
            themes_.applyTheme(i);
            viz_.setColor(themes_.accentColor());
        }
    }
}

void UIManager::openThemeEditor() {
    edit_theme_ = themes_.current();
    if (edit_theme_.builtin) edit_theme_.name += " Copy";
    std::snprintf(edit_name_buf_, sizeof(edit_name_buf_), "%s", edit_theme_.name.c_str());
    show_theme_editor_ = true;
}

// Live theme editor: every change is previewed immediately
void UIManager::drawThemeEditor() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    bool open = true;
    if (!ImGui::Begin("Theme Editor", &open,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto color = [&](const char* label, ImU32& c) {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        if (ImGui::ColorEdit3(label, &v.x, ImGuiColorEditFlags_NoInputs)) {
            c = ImGui::ColorConvertFloat4ToU32({v.x, v.y, v.z, 1.f});
            changed = true;
        }
    };

    ImGui::SetNextItemWidth(S(220.f));
    ImGui::InputText("Name", edit_name_buf_, sizeof(edit_name_buf_));
    ImGui::Separator();
    color("Accent",           edit_theme_.accent);   ImGui::SameLine(S(170.f));
    color("Accent 2",         edit_theme_.accent2);
    color("Background",       edit_theme_.bg);       ImGui::SameLine(S(170.f));
    color("Panels",           edit_theme_.panel);
    color("Controls",         edit_theme_.frame);    ImGui::SameLine(S(170.f));
    color("Text",             edit_theme_.text);
    color("Dim text",         edit_theme_.text_dim);
    ImGui::SetNextItemWidth(S(220.f));
    changed |= ImGui::SliderFloat("Roundness", &edit_theme_.rounding, 0.f, 12.f, "%.0f px");
    changed |= ImGui::Checkbox("Control borders", &edit_theme_.borders);

    if (changed) {
        themes_.previewTheme(edit_theme_);
        viz_.setColor(edit_theme_.accent);
    }

    ImGui::Separator();
    const float bw = S(110.f);
    if (ImGui::Button("Save", {bw, 0})) {
        edit_theme_.name = edit_name_buf_;
        themes_.saveCustom(edit_theme_);
        viz_.setColor(themes_.accentColor());
        open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {bw, 0})) open = false;
    const bool custom = !themes_.current().builtin;
    if (custom) {
        ImGui::SameLine();
        if (ImGui::Button("Delete theme", {bw, 0})) {
            themes_.deleteCustom(themes_.currentIndex());
            open = false;
        }
    }
    ImGui::TextDisabled("Saved themes: %s", "themes.ini in your PulseAmp settings folder");
    ImGui::End();

    if (!open) {
        show_theme_editor_ = false;
        themes_.applyTheme(themes_.currentIndex()); // drop an unsaved preview
        viz_.setColor(themes_.accentColor());
    }
}

// ─── Fullscreen / scale ───────────────────────────────────────────────────────
void UIManager::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    if (SDL_Window* win = SDL_GL_GetCurrentWindow())
        SDL_SetWindowFullscreen(win, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    last_activity_ = ImGui::GetTime();
}

void UIManager::devShow(const std::string& what_list) {
    // Comma-separated list, e.g. "milkdrop,fullscreen"
    size_t start = 0;
    while (start <= what_list.size()) {
        size_t end = what_list.find(',', start);
        if (end == std::string::npos) end = what_list.size();
        const std::string what = what_list.substr(start, end - start);
        start = end + 1;

        if (what == "about")      show_about_ = true;
        if (what == "themes")     openThemeEditor();
        if (what == "settings")   { show_sidebar_ = true; sidebar_tab_ = 3; }
        if (what == "online")     { show_sidebar_ = true; sidebar_tab_ = 1; }
        if (what == "milkdrop")   viz_.setMode(Visualizer::Mode::MilkDrop);
        if (what == "fullscreen") { fullscreen_ = true; last_activity_ = -10.0; }
        if (what.rfind("skin:", 0) == 0) { loadSkin(what.substr(5)); if (!skin_mode_) toggleSkinMode(); }
        if (what.rfind("search:", 0) == 0 || what.rfind("scsearch:", 0) == 0) {
            bool sc = what[0] == 's' && what[1] == 'c';
            std::snprintf(online_query_, sizeof(online_query_), "%s", what.substr(what.find(':') + 1).c_str());
            online_src_ = sc ? 1 : 0;
            show_sidebar_ = true; sidebar_tab_ = 1;
            online_.search(sc ? OnlineSource::SoundCloud : OnlineSource::YouTube, online_query_);
            search_started_ = 0.0;
        }
    }
}

void UIManager::setScale(float s) {
    ui_scale_ = s;
    themes_.setScale(s);
}

// ─── Chapters tab ─────────────────────────────────────────────────────────────
void UIManager::drawChaptersTab() {
    const auto& chaps = player_.getChapters();
    if (chaps.empty()) {
        ImGui::TextDisabled("No chapters in this file.\n\n"
                            "PulseAmp auto-detects chapters\nfrom container metadata.");
        return;
    }
    double cur = player_.getCurrentTime();
    for (auto& ch : chaps) {
        bool active = (cur >= ch.start && cur < ch.end);
        char buf[256];
        snprintf(buf, sizeof(buf), "  %s  [%s]",
                 ch.title.c_str(), formatTime(ch.start).c_str());
        ImGui::PushID(&ch);
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, {0.2f,1.f,0.4f,1.f});
        if (ImGui::Selectable(buf, active))
            player_.seek(ch.start);
        if (active) ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

// ─── Info overlay ─────────────────────────────────────────────────────────────
void UIManager::drawInfoOverlay(double /*time*/) {
    const PlaylistEntry* pe = playlist_.current();
    if (!pe) return;

    ImGui::SetNextWindowBgAlpha(overlay_alpha_ * 0.7f);
    ImGui::SetNextWindowPos({S(12.f), menubar_h_ + S(12.f)});
    ImGui::SetNextWindowSize({S(280.f), 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(6.f));
    if (ImGui::Begin("##overlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored({1.f,1.f,1.f, overlay_alpha_}, "%s", pe->title.c_str());
        if (pe->duration > 0)
            ImGui::TextColored({0.7f,0.7f,0.7f,overlay_alpha_},
                "Duration: %s", formatTime(pe->duration).c_str());
        if (displayed_bpm_ > 0)
            ImGui::TextColored({0.7f,0.7f,0.7f,overlay_alpha_},
                "BPM: %.0f", displayed_bpm_);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
void UIManager::playEntry(int idx) {
    playlist_.setIndex(idx);
    const PlaylistEntry* pe = playlist_.current();
    if (!pe) return;

    // Save position of previous file
    if (!playlist_.entries().empty()) {
        const auto& cur_path = player_.getFilePath();
        if (!cur_path.empty())
            playlist_.savePosition(cur_path, player_.getCurrentTime());
    }

    player_.close();
    audio_.close();
    video_.destroy();
    waveform_ready_ = false;
    online_status_.clear();

    // URLs: resolve (yt-dlp) and open on a worker thread - this takes seconds
    if (isUrl(pe->path)) {
        if (open_job_.valid()) stale_jobs_.push_back(std::move(open_job_));
        open_job_idx_   = playlist_.getIndex();
        open_job_title_ = pe->title;
        const std::string url  = pe->path;
        const bool page        = isOnlinePageUrl(url);
        const bool want_video  = online_video_;
        open_job_ = std::async(std::launch::async, [this, url, page, want_video] {
            OpenResult r;
            if (page) {
                r.rs = online_.resolveBlocking(url, want_video, r.error);
                if (!r.error.empty()) return r;
            } else {
                r.rs.page_url = r.rs.stream_url = url;   // direct media / radio stream
            }
            OpenOptions o;
            o.audio_url    = r.rs.audio_url;
            o.http_headers = r.rs.http_headers;
            o.source_id    = url;
            r.media = Player::prepare(r.rs.stream_url, o);
            if (!r.media) r.error = "Could not open the stream";
            return r;
        });
        waveform_path_ = url;            // no waveform scan for streams
        waveform_peaks_.clear();
        overlay_timer_ = 0; overlay_alpha_ = 1.f;
        return;
    }

    if (!player_.open(pe->path)) return;
    onOpened(pe);
}

// Set up output, resume position and waveform once the player has opened pe
void UIManager::onOpened(const PlaylistEntry* pe) {
    audio_.open(player_.getSampleRate(), player_.getChannels(),
                &player_.getAudioBuffer());
    if (player_.hasVideo())
        video_.init(player_.getVideoWidth(), player_.getVideoHeight());

    // Smart resume
    if (smart_resume_) {
        auto pos = playlist_.getSavedPosition(pe->path);
        if (pos) player_.seek(*pos);
    }

    // Kick off waveform generation (local files only)
    if (!isUrl(pe->path) && waveform_path_ != pe->path) {
        waveform_path_ = pe->path;
        waveform_peaks_.clear();
        { std::lock_guard lk(waveform_mu_); waveform_pending_ready_ = false; }
        waveform_.generate(pe->path, 512, [this](std::vector<float> peaks){
            std::lock_guard lk(waveform_mu_);
            waveform_pending_ = std::move(peaks);
            waveform_pending_ready_ = true;
        });
    }

    // Reset overlay
    overlay_timer_ = 0; overlay_alpha_ = 1.f;
}

void UIManager::addToPlaylist(const std::string& path) {
    if (isUrl(path)) { playlist_.addFile(path, path); return; }

    // Accepted extensions
    static const char* EXTS[] = {
        ".mkv",".mp4",".avi",".mov",".webm",
        ".mp3",".flac",".wav",".ogg",".aac",".opus",".m4a",
        nullptr
    };
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (int i = 0; EXTS[i]; ++i)
        if (ext == EXTS[i]) {
            playlist_.addFile(path);
            return;
        }
    // If directory, recurse
    if (fs::is_directory(path))
        for (auto& de : fs::recursive_directory_iterator(path))
            if (de.is_regular_file()) addToPlaylist(de.path().string());
}

void UIManager::openFile() {
#ifdef _WIN32
    char buf[4096] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter =
        "Media Files\0*.mkv;*.mp4;*.avi;*.mov;*.mp3;*.flac;*.wav;*.ogg;*.aac;*.opus\0"
        "All Files\0*.*\0";
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = sizeof(buf);
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (GetOpenFileNameA(&ofn)) {
        addToPlaylist(buf);
        if (player_.getState() == PlayerState::Stopped)
            playEntry((int)playlist_.entries().size()-1);
    }
#else
    // On Linux/macOS: use zenity if available, else SDL_ShowSimpleMessageBox
    FILE* f = popen("zenity --file-selection --multiple --separator='\\n' "
                    "--file-filter='Media files|*.mkv *.mp4 *.avi *.mp3 *.flac *.wav *.ogg' 2>/dev/null", "r");
    if (!f) return;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (buf[0]) addToPlaylist(buf);
    }
    pclose(f);
    if (player_.getState() == PlayerState::Stopped && !playlist_.entries().empty())
        playEntry(0);
#endif
}

void UIManager::openFolder() {
#ifdef _WIN32
    BROWSEINFOA bi{};
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pid = SHBrowseForFolderA(&bi);
    if (pid) {
        char buf[MAX_PATH];
        SHGetPathFromIDListA(pid, buf);
        CoTaskMemFree(pid);
        addToPlaylist(buf);
    }
#else
    FILE* f = popen("zenity --file-selection --directory 2>/dev/null", "r");
    if (!f) return;
    char buf[4096];
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (buf[0]) addToPlaylist(buf);
    }
    pclose(f);
#endif
}

std::string UIManager::formatTime(double s) const {
    if (s < 0) s = 0;
    int h = (int)(s / 3600);
    int m = (int)(s / 60) % 60;
    int sc = (int)s % 60;
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, sc);
    else       snprintf(buf, sizeof(buf), "%d:%02d", m, sc);
    return buf;
}

// ─── Online (YouTube / SoundCloud) ────────────────────────────────────────────
void UIManager::playOnline(const std::string& url, const std::string& title, double dur) {
    playlist_.addFile(url, title.empty() ? url : title, dur);
    playEntry(playlist_.size() - 1);
}

void UIManager::pollBackgroundWork() {
    std::vector<OnlineResult> res;
    std::string err, msg;
    if (online_.pollSearch(res, err)) {
        for (auto& r : res) { Fonts::noteText(r.title); Fonts::noteText(r.uploader); }
        online_results_ = std::move(res);
        online_error_ = err;
    }
    if (online_.pollUpdate(msg)) ytdlp_msg_ = msg;

    using namespace std::chrono_literals;
    // Superseded opens: drop them once finished (closing their streams)
    stale_jobs_.erase(std::remove_if(stale_jobs_.begin(), stale_jobs_.end(),
        [](auto& f) { return f.wait_for(0s) == std::future_status::ready; }), stale_jobs_.end());

    // Prefetch: resolve the next online track while this one plays
    if (prefetch_job_.valid() && prefetch_job_.wait_for(0s) == std::future_status::ready)
        prefetch_job_.get();
    if (!prefetch_job_.valid() && !open_job_.valid() && online_.available() &&
        player_.getState() == PlayerState::Playing) {
        int ni = playlist_.peekNextIndex();
        if (ni >= 0 && ni != playlist_.getIndex()) {
            const std::string url = playlist_.entries()[ni].path;
            const bool video = online_video_;
            if (isOnlinePageUrl(url) && !online_.isCached(url, video))
                prefetch_job_ = std::async(std::launch::async, [this, url, video] {
                    std::string err;
                    online_.resolveBlocking(url, video, err);
                });
        }
    }

    if (open_job_.valid() && open_job_.wait_for(0s) == std::future_status::ready) {
        OpenResult r = open_job_.get();
        if (open_job_idx_ == playlist_.getIndex()) {
            if (r.media && player_.start(std::move(r.media))) {
                Fonts::noteText(r.rs.title);
                playlist_.setInfo(open_job_idx_, r.rs.title, r.rs.duration);
                onOpened(playlist_.current());
            } else {
                online_status_ = "Can't play: " + (r.error.empty() ? std::string("unknown error") : r.error);
            }
        }
        open_job_idx_ = -1;
    }
}

void UIManager::drawOnlineTab() {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    if (!online_.available()) {
        ImGui::TextWrapped("YouTube and SoundCloud need yt-dlp, a free command-line tool.");
        ImGui::Spacing();
        ImGui::TextWrapped("Download yt-dlp.exe from github.com/yt-dlp/yt-dlp/releases and put it "
                           "next to PulseAmp.exe (or anywhere on your PATH).");
        if (ImGui::Button("Check again", {-1, 0})) online_.rescan();
        ImGui::Spacing();
        ImGui::SeparatorText("Direct link");
    } else {
        ImGui::RadioButton("YouTube", &online_src_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("SoundCloud", &online_src_, 1);

        const float bw = S(70.f);
        Fonts::noteText(online_query_);   // typed CJK text (IME) needs glyphs too
        ImGui::SetNextItemWidth(-bw - spacing);
        bool enter = ImGui::InputTextWithHint("##q", "Search...", online_query_, sizeof(online_query_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool go = ImGui::Button("Search", {bw, 0}) || enter;
        if (go && online_query_[0] && !online_.searching()) {
            online_.search(online_src_ == 0 ? OnlineSource::YouTube : OnlineSource::SoundCloud,
                           online_query_);
            search_started_ = ImGui::GetTime();
        }
        if (online_src_ == 0)
            ImGui::Checkbox("Play video (not just audio)", &online_video_);

        if (online_.searching())            // SoundCloud searches can take ~15-30 s
            ImGui::TextDisabled("Searching %s... %.0f s", online_src_ == 0 ? "YouTube" : "SoundCloud",
                                ImGui::GetTime() - search_started_);
        else if (!online_error_.empty())    ImGui::TextDisabled("%s", online_error_.c_str());

        // Results: double-click to play, right-click for more
        ImGui::BeginChild("##results", {-1, -S(150.f)}, true);
        for (int i = 0; i < (int)online_results_.size(); ++i) {
            const auto& r = online_results_[i];
            ImGui::PushID(i);
            if (ImGui::Selectable(r.title.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(0))
                playOnline(r.url, r.title, r.duration);
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("PA_ONLINE", &i, sizeof(int));
                ImGui::Text("%s", r.title.c_str());
                ImGui::TextDisabled("Drop on the playlist");
                ImGui::EndDragDropSource();
            } else if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n(double-click to play, drag to the playlist)", r.url.c_str());
            }
            if (ImGui::BeginPopupContextItem("##res")) {
                if (ImGui::MenuItem("Play"))            playOnline(r.url, r.title, r.duration);
                if (ImGui::MenuItem("Add to playlist")) playlist_.addFile(r.url, r.title, r.duration);
                if (ImGui::MenuItem("Copy link"))       ImGui::SetClipboardText(r.url.c_str());
                ImGui::EndPopup();
            }
            std::string sub = r.uploader;
            if (r.duration > 0) sub += (sub.empty() ? "" : "  ") + formatTime(r.duration);
            if (!sub.empty()) ImGui::TextDisabled("   %s", sub.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SeparatorText("Link");
    }

    // Paste a link (YouTube / SoundCloud page, or any direct media / radio URL)
    if (focus_link_) { ImGui::SetKeyboardFocusHere(); focus_link_ = false; }
    Fonts::noteText(online_link_);
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##link", "Paste a YouTube / SoundCloud / stream URL",
                                          online_link_, sizeof(online_link_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    std::string link = online_link_;
    while (!link.empty() && std::isspace((unsigned char)link.back())) link.pop_back();
    const bool valid = isUrl(link);
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Play", {half, 0}) || (enter && valid)) playOnline(link, "", 0.0);
    ImGui::SameLine();
    if (ImGui::Button("Add to playlist", {half, 0})) playlist_.addFile(link, link);
    ImGui::EndDisabled();

    if (open_job_.valid())           ImGui::TextDisabled("Loading %s ...", open_job_title_.c_str());
    else if (!online_status_.empty()) ImGui::TextWrapped("%s", online_status_.c_str());

    if (online_.available()) {
        ImGui::SeparatorText("yt-dlp");
        ImGui::BeginDisabled(online_.updating());
        if (ImGui::Button(online_.updating() ? "Updating..." : "Update yt-dlp")) online_.update();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sites change often: update yt-dlp if YouTube or SoundCloud stop working");
        if (!ytdlp_msg_.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", ytdlp_msg_.c_str()); }
    }
}

// ─── MilkDrop ─────────────────────────────────────────────────────────────────
void UIManager::drawMilkDrop(float x, float y, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!milkdrop_.init() || milkdrop_.presetCount() == 0) {
        // Explain why there is nothing to show
        std::string msg = milkdrop_.error().empty() ? "MilkDrop unavailable" : milkdrop_.error();
        if (milkdrop_.ok() && milkdrop_.presetCount() == 0 && !milkdrop_.presetDirs().empty())
            msg += "\nDownload PulseAmp-milkdrop-presets.zip from\n"
                   "cursedprograms.github.io/PulseAmp-Media-Player\n"
                   "and extract it into the PulseAmp folder, or put .milk files in:\n" + milkdrop_.presetDirs()[0];
        Fonts::noteText(msg);
        ImVec2 ts = ImGui::CalcTextSize(msg.c_str());
        dl->AddText({x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), msg.c_str());
        return;
    }

    milkdrop_w_ = w;
    milkdrop_h_ = h;
    if (milkdrop_.texture()) {
        // Texture was copied bottom-up from the framebuffer: flip V.
        // Drawn directly (not a widget) so double-click-to-fullscreen still works.
        dl->AddImage((ImTextureID)(intptr_t)milkdrop_.texture(), {x, y}, {x + w, y + h},
                     {0, 1}, {1, 0});
    }

    // Controls when hovered: previous / next / random + preset name
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        const float bh = ImGui::GetFrameHeight();
        ImGui::SetCursorPos({S(6.f), h - bh - S(6.f)});
        ImGui::PushStyleColor(ImGuiCol_Button, {0.f, 0.f, 0.f, 0.5f});
        if (ImGui::Button("<##mdprev")) milkdrop_.previous();
        ImGui::SameLine();
        if (ImGui::Button(">##mdnext")) milkdrop_.next();
        ImGui::SameLine();
        if (ImGui::Button("Random##mdrand")) milkdrop_.random();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        std::string name = milkdrop_.presetName();
        Fonts::noteText(name);
        ImGui::TextUnformatted(name.c_str());
    }
}

void UIManager::preRenderGL(float fb_scale) {
    if (milkdrop_w_ > 0.f && milkdrop_h_ > 0.f && milkdrop_.ok())
        milkdrop_.renderToTexture((int)(milkdrop_w_ * fb_scale), (int)(milkdrop_h_ * fb_scale));
}

// ─── External drag and drop ───────────────────────────────────────────────────
void UIManager::beginExternalDrop() {
    drop_start_ = playlist_.size();
    drop_on_playlist_ = false;
    drop_insert_at_ = -1;
    // Where is the cursor? (SDL doesn't send mouse moves during an OS drag)
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    if (!win || skin_mode_ || !show_sidebar_ || sidebar_tab_ != 0 || fullscreen_) return;
    int gx, gy, wx, wy;
    SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(win, &wx, &wy);
    const float mx = (float)(gx - wx), my = (float)(gy - wy);
    if (mx < pl_list_min_.x || mx >= pl_list_max_.x || my < pl_list_min_.y || my >= pl_list_max_.y) return;
    drop_on_playlist_ = true;
    drop_insert_at_ = (int)pl_row_top_.size();          // below the last row: append
    for (int i = 0; i < (int)pl_row_top_.size(); ++i)
        if (my < pl_row_top_[i] + (i + 1 < (int)pl_row_top_.size() ? pl_row_top_[i + 1] - pl_row_top_[i]
                                                                       : ImGui::GetFrameHeight()) * 0.5f)
            { drop_insert_at_ = i; break; }
}

void UIManager::finishExternalDrop() {
    if (drop_start_ < 0) return;
    const int first = drop_start_;
    drop_start_ = -1;
    if (playlist_.size() <= first) return;              // nothing usable was dropped

    if (drop_on_playlist_) {
        // Dropped on the playlist: insert where it landed; only start if idle
        insertEntries(first, drop_insert_at_);
        if (player_.getState() == PlayerState::Stopped && !open_job_.valid())
            playEntry(std::min(drop_insert_at_, playlist_.size() - 1));
    } else {
        // Dropped on the video / visualizer / skin: play it now
        playEntry(first);
    }
}

// Move entries [first_new, end) so they start at row `at` (append if at is out of range)
void UIManager::insertEntries(int first_new, int at) {
    const int n = playlist_.size();
    if (at < 0 || at >= first_new) return;              // already at the end
    for (int k = 0; k < n - first_new; ++k)
        playlist_.move(first_new + k, at + k);
}
