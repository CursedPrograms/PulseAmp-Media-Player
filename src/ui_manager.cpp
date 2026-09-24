// ─── ui_manager.cpp ───────────────────────────────────────────────────────────
#include "ui_manager.h"
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
    "BPM Pulse", "Particle Storm"
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
            case SDLK_m:     player_.setMuted(!player_.isMuted()); break;
            case SDLK_f: {
                fullscreen_ = !fullscreen_;
                if (SDL_Window* win = SDL_GetWindowFromID(e.key.windowID))
                    SDL_SetWindowFullscreen(win, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                break;
            }
            case SDLK_n:     { auto* ne = playlist_.next();
                               if (ne) playEntry(playlist_.getIndex()); break; }
            case SDLK_p:     { auto* pr = playlist_.prev();
                               if (pr) playEntry(playlist_.getIndex()); break; }
            case SDLK_TAB:   show_sidebar_ = !show_sidebar_; break;
            case SDLK_v:     show_viz_ = !show_viz_; break;
            case SDLK_ESCAPE: return false;
            default: break;
        }
    }

    // Drag-and-drop files
    if (e.type == SDL_DROPFILE) {
        std::string path = e.drop.file;
        SDL_free(e.drop.file);
        addToPlaylist(path);
        if (player_.getState() == PlayerState::Stopped)
            playEntry((int)playlist_.entries().size() - 1);
    }

    return true;
}

// ─── Main render ──────────────────────────────────────────────────────────────
void UIManager::render(int win_w, int win_h, double time) {
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

    // Feed visualizer
    if (player_.getState() == PlayerState::Playing ||
        player_.getState() == PlayerState::Paused)
        viz_.feed(player_.getAudioBuffer());

    // Info overlay fade
    overlay_timer_ += ImGui::GetIO().DeltaTime;
    if (overlay_timer_ > 3.0f)
        overlay_alpha_ = std::max(0.f, overlay_alpha_ - ImGui::GetIO().DeltaTime * 0.5f);

    // ── Layout ────────────────────────────────────────────────────────────────
    const float MENUBAR_H  = ImGui::GetFrameHeight();
    const float TRANSPORT_H = 72.f;
    const float SIDEBAR_W  = show_sidebar_ ? 310.f : 0.f;
    const float VIZ_H      = show_viz_
        ? std::max(80.f, (win_h - MENUBAR_H - TRANSPORT_H) * viz_height_pct_)
        : 0.f;
    const float CONTENT_W  = win_w - SIDEBAR_W;
    const float CONTENT_H  = win_h - MENUBAR_H - TRANSPORT_H;
    const float VIDEO_H    = show_video_ ? CONTENT_H - VIZ_H : 0.f;

    drawMenuBar();

    float cy = MENUBAR_H;

    // Video panel
    if (show_video_ && VIDEO_H > 0)
        drawVideoPanel(0, cy, CONTENT_W, VIDEO_H);
    cy += VIDEO_H;

    // Visualizer panel
    if (show_viz_ && VIZ_H > 0)
        drawVizPanel(0, cy, CONTENT_W, VIZ_H, time);
    cy += VIZ_H;

    // Transport bar
    drawTransportBar(0, win_h - TRANSPORT_H, (float)win_w, TRANSPORT_H);

    // Sidebar
    if (show_sidebar_)
        drawSidebar(CONTENT_W, MENUBAR_H, SIDEBAR_W, CONTENT_H, time);

    // Info overlay
    if (info_overlay_ && overlay_alpha_ > 0.01f)
        drawInfoOverlay(time);
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────
void UIManager::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open File\t(O)"))   openFile();
        if (ImGui::MenuItem("Open Folder"))      openFolder();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit\t(Esc)")) {}
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
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            for (auto& td : themes_.themes()) {
                bool sel = (td.name == themes_.themes()[(int)themes_.currentTheme()].name);
                if (ImGui::MenuItem(td.name.c_str(), nullptr, sel)) {
                    Theme t = (Theme)(&td - themes_.themes().data());
                    themes_.applyTheme(t);
                    viz_.setColor(themes_.accentColor());
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Visualizer Mode")) {
            for (int i = 0; i < 5; ++i) {
                bool sel = ((int)viz_.getMode() == i);
                if (ImGui::MenuItem(VIZ_NAMES[i], nullptr, sel))
                    viz_.setMode((Visualizer::Mode)i);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Converter")) { show_sidebar_ = true; sidebar_tab_ = 1; }
        if (ImGui::MenuItem("Settings"))  { show_sidebar_ = true; sidebar_tab_ = 2; }
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
        ImVec2 center = { x + w*0.5f, y + h*0.5f };
        ImGui::GetWindowDrawList()->AddText(
            {center.x - 80.f, center.y - 8.f},
            IM_COL32(80,80,80,255), "Drop a file here to play");
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.04f,0.04f,0.04f,1.f});
    ImGui::Begin("##viz", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);

    // Cycle mode button (top-right)
    ImGui::SetCursorPos({w - 130.f, 4.f});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.f,0.f,0.f,0.4f});
    if (ImGui::Button(VIZ_NAMES[(int)viz_.getMode()], {125.f, 18.f})) {
        int next = ((int)viz_.getMode() + 1) % 5;
        viz_.setMode((Visualizer::Mode)next);
    }
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    viz_.render(dl, {x, y}, {w, h}, displayed_bpm_, time);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ─── Waveform seek bar (unique feature) ───────────────────────────────────────
void UIManager::drawWaveformSeekBar(float x, float y, float w, float h) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 bg  = IM_COL32(20,20,20,200);
    ImU32 acc = themes_.accentColor();
    ImU32 acc_dim = IM_COL32(
        ((acc>>IM_COL32_R_SHIFT)&0xFF)/3,
        ((acc>>IM_COL32_G_SHIFT)&0xFF)/3,
        ((acc>>IM_COL32_B_SHIFT)&0xFF)/3, 200);

    dl->AddRectFilled({x,y},{x+w,y+h}, bg, 3.f);

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
            dl->AddRectFilled({x,y},{x+w*frac,y+h}, acc_dim, 3.f);
        }
    }

    // Playhead
    if (dur > 0) {
        float fx = x + (float)(cur/dur)*w;
        dl->AddLine({fx,y},{fx,y+h}, IM_COL32(255,255,255,200), 2.f);
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.f, 6.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.08f,0.08f,0.10f,1.f});
    ImGui::Begin("##transport", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav);

    // Row 1: Seek bar
    float bar_y = y + 6.f;
    float bar_h = 18.f;
    drawWaveformSeekBar(x + 8.f, bar_y, w - 16.f, bar_h);

    ImGui::SetCursorPosY(bar_h + 10.f);

    // Row 2: Controls
    float btn_w = 36.f;
    bool playing = player_.getState() == PlayerState::Playing;
    ImU32 acc = themes_.accentColor();
    ImGui::PushStyleColor(ImGuiCol_Button,
        ImVec4(((acc>>IM_COL32_R_SHIFT)&0xFF)/255.f*0.4f,
               ((acc>>IM_COL32_G_SHIFT)&0xFF)/255.f*0.4f,
               ((acc>>IM_COL32_B_SHIFT)&0xFF)/255.f*0.4f, 1.f));

    if (ImGui::Button("|<##prev",{btn_w,22.f})) {
        auto* pr = playlist_.prev(); if(pr) playEntry(playlist_.getIndex());
    }
    ImGui::SameLine(0,4);
    if (ImGui::Button(playing ? "||##pause" : "> ##play", {btn_w+10.f,22.f})) {
        if (playing) player_.pause(); else player_.play();
    }
    ImGui::SameLine(0,4);
    if (ImGui::Button("[]##stop",{btn_w,22.f})) player_.stop();
    ImGui::SameLine(0,4);
    if (ImGui::Button(">|##next",{btn_w,22.f})) {
        auto* ne = playlist_.next(); if(ne) playEntry(playlist_.getIndex());
    }
    ImGui::PopStyleColor();

    // Shuffle / Repeat
    ImGui::SameLine(0,12);
    bool shuf = playlist_.getShuffle();
    if (ImGui::Button(shuf ? "[S]" : " S ", {28.f,22.f})) playlist_.setShuffle(!shuf);
    ImGui::SameLine(0,4);
    RepeatMode rep = playlist_.getRepeat();
    const char* rep_lbl = rep==RepeatMode::All ? "[R]" : rep==RepeatMode::One ? "[1]" : " R ";
    if (ImGui::Button(rep_lbl, {28.f,22.f})) {
        playlist_.setRepeat(rep==RepeatMode::None ? RepeatMode::All :
                            rep==RepeatMode::All  ? RepeatMode::One : RepeatMode::None);
    }

    // Time display
    ImGui::SameLine(0,16);
    double cur = player_.getCurrentTime(), dur = player_.getDuration();
    ImGui::Text("%s / %s", formatTime(cur).c_str(), formatTime(dur).c_str());

    // Volume
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    float vol_pct = volume_ * 100.f;
    if (ImGui::SliderFloat("##vol", &vol_pct, 0.f, 100.f, "Vol %.0f%%", ImGuiSliderFlags_None)) {
        volume_ = std::clamp(vol_pct / 100.f, 0.f, 1.f);
        player_.setVolume(volume_);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) volume_ = 1.f, player_.setVolume(1.f);

    // File name
    const PlaylistEntry* pe = playlist_.current();
    if (pe) {
        ImGui::SameLine(0,16);
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.f,6.f});
    ImGui::Begin("##sidebar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Tabs
    const char* tabs[] = {"Playlist","Convert","Settings","Chapters"};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f,3.f});
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine(0, 2.f);
        bool active = (sidebar_tab_ == i);
        if (active) {
            ImU32 a = themes_.accentColor();
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(((a>>IM_COL32_R_SHIFT)&0xFF)/255.f*0.5f,
                       ((a>>IM_COL32_G_SHIFT)&0xFF)/255.f*0.5f,
                       ((a>>IM_COL32_B_SHIFT)&0xFF)/255.f*0.5f,1.f));
        }
        if (ImGui::Button(tabs[i], {(w-28.f)/4.f, 22.f})) sidebar_tab_ = i;
        if (active) ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::Separator();

    switch (sidebar_tab_) {
        case 0: drawPlaylistTab();  break;
        case 1: drawConverterTab(); break;
        case 2: drawSettingsTab();  break;
        case 3: drawChaptersTab();  break;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ─── Playlist tab ─────────────────────────────────────────────────────────────
void UIManager::drawPlaylistTab() {
    auto& entries = playlist_.entries();
    int cur = playlist_.getIndex();

    if (ImGui::Button("+ Add File", {-1,22.f})) openFile();

    ImGui::BeginChild("##pl_list", {-1, -28.f}, false);
    for (int i = 0; i < (int)entries.size(); ++i) {
        const auto& e = entries[i];
        bool sel = (i == cur);
        ImGui::PushID(i);
        if (sel) {
            ImU32 a = themes_.accentColor();
            ImGui::PushStyleColor(ImGuiCol_Header,
                ImVec4(((a>>IM_COL32_R_SHIFT)&0xFF)/255.f*0.35f,
                       ((a>>IM_COL32_G_SHIFT)&0xFF)/255.f*0.35f,
                       ((a>>IM_COL32_B_SHIFT)&0xFF)/255.f*0.35f,1.f));
        }
        char label[256];
        snprintf(label, sizeof(label), "%s##pl%d", e.title.c_str(), i);
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) playEntry(i);
        }
        if (sel) ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("##plctx")) {
            if (ImGui::MenuItem("Play"))       playEntry(i);
            if (ImGui::MenuItem("Remove"))     playlist_.removeAt(i);
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", e.path.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Clear", {-1,20.f})) playlist_.clear();
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
    if (ImGui::Button("Browse Source", {-1,20.f})) {
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
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("##abr", &conv_audio_kbps_);
    ImGui::TextDisabled("Video kbps:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("##vbr", &conv_vid_kbps_);
    ImGui::Checkbox("Strip video (audio only)", &conv_strip_vid_);
    ImGui::Checkbox("Strip audio (video only)", &conv_strip_aud_);

    ImGui::Spacing();
    ImGui::Separator();

    bool running = conv_.isRunning();
    if (running) {
        ImGui::ProgressBar((float)prog.progress, {-1,18.f});
        ImGui::TextDisabled("%.0f%%  %s / %s",
            prog.progress*100,
            formatTime(prog.current_time).c_str(),
            formatTime(prog.duration).c_str());
        if (ImGui::Button("Cancel", {-1,22.f})) conv_.cancel();
    } else {
        if (prog.done && !prog.error)
            ImGui::TextColored({0.2f,1.f,0.4f,1.f}, "Done!");
        if (prog.error)
            ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "Error: %s", prog.error_msg.c_str());

        if (ImGui::Button("Convert", {-1,22.f})) {
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
    ImGui::TextDisabled("─ Playback ─");
    ImGui::Checkbox("Smart Resume",     &smart_resume_);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remembers playback position for every file");
    ImGui::Checkbox("Auto-advance",     &auto_advance_);
    ImGui::Spacing();

    ImGui::TextDisabled("─ Visualizer ─");
    ImGui::TextDisabled("Mode:");
    ImGui::SetNextItemWidth(-1);
    int mode = (int)viz_.getMode();
    if (ImGui::Combo("##vizmode", &mode, VIZ_NAMES, 5))
        viz_.setMode((Visualizer::Mode)mode);

    ImGui::TextDisabled("Viz Height:");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##vizh", &viz_height_pct_, 0.1f, 0.5f, "%.0f%%");

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

    ImGui::Spacing();
    ImGui::TextDisabled("─ Spatial Audio ─");
    bool spa = spatial_.isEnabled();
    if (ImGui::Checkbox("Stereo Widener (headphones)", &spa)) spatial_.setEnabled(spa);
    if (spa) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##spw", &spatial_width_, 0.f, 2.f, "Width %.2f");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("─ Theme ─");
    for (auto& td : themes_.themes()) {
        bool sel = (td.name == themes_.themes()[(int)themes_.currentTheme()].name);
        if (ImGui::RadioButton(td.name.c_str(), sel)) {
            Theme t = (Theme)(&td - themes_.themes().data());
            themes_.applyTheme(t);
            viz_.setColor(themes_.accentColor());
        }
    }
}

// ─── Chapters tab ─────────────────────────────────────────────────────────────
void UIManager::drawChaptersTab() {
    const auto& chaps = player_.getChapters();
    if (chaps.empty()) {
        ImGui::TextDisabled("No chapters in this file.\n\n"
                            "NovPlayer auto-detects chapters\nfrom container metadata.");
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
    ImGui::SetNextWindowPos({12.f, ImGui::GetFrameHeight() + 12.f});
    ImGui::SetNextWindowSize({280.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
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

    if (!player_.open(pe->path)) return;

    audio_.open(player_.getSampleRate(), player_.getChannels(),
                &player_.getAudioBuffer());
    if (player_.hasVideo())
        video_.init(player_.getVideoWidth(), player_.getVideoHeight());

    // Smart resume
    if (smart_resume_) {
        auto pos = playlist_.getSavedPosition(pe->path);
        if (pos) player_.seek(*pos);
    }

    // Kick off waveform generation
    if (waveform_path_ != pe->path) {
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
