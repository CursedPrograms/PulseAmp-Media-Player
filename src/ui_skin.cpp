// ─── ui_skin.cpp ──────────────────────────────────────────────────────────────
// Classic mode: the player as a compact, skinned, shaped window.
// Part of UIManager (see ui_manager.h); Winamp and PulseAmp skin rendering.
// ─────────────────────────────────────────────────────────────────────────────
#include "ui_manager.h"
#include "skin_window.h"
#include "fonts.h"
#include "paths.h"
#include "skin_draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace {

ImTextureID texId(const SkinImage* img) { return (ImTextureID)(intptr_t)img->tex; }

// Winamp text.bmp: 5x6 glyphs, 31 columns x 3 rows
bool winampGlyph(unsigned char c, int& col, int& row) {
    c = (unsigned char)std::tolower(c);
    if (c >= 'a' && c <= 'z') { col = c - 'a'; row = 0; return true; }
    if (c >= '0' && c <= '9') { col = c - '0'; row = 1; return true; }
    static const struct { char ch; int col, row; } map[] = {
        {'"',26,0},{'@',27,0},{' ',30,0},
        {'.',11,1},{':',12,1},{'(',13,1},{')',14,1},{'-',15,1},{'\'',16,1},{'!',17,1},{'_',18,1},
        {'+',19,1},{'\\',20,1},{'/',21,1},{'[',22,1},{']',23,1},{'^',24,1},{'&',25,1},{'%',26,1},
        {',',27,1},{'=',28,1},{'$',29,1},{'#',30,1},
        {'?',3,2},{'*',4,2},{'{',13,1},{'}',14,1},{'<',22,1},{'>',23,1},{'~',15,1},{'|',21,1} };
    for (auto& m : map) if (m.ch == (char)c) { col = m.col; row = m.row; return true; }
    col = 30; row = 0;   // unknown: space
    return false;
}

} // namespace

// ─── Skins on disk ────────────────────────────────────────────────────────────
std::vector<std::string> UIManager::findSkins() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto dir : { fs::path(exeDir()) / "skins", fs::path(appDataDir()) / "skins" }) {
        fs::create_directories(dir, ec);
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (e.is_regular_file(ec) && (ext == ".wsz" || ext == ".zip" || ext == ".paskin"))
                out.push_back(e.path().u8string());
            else if (e.is_directory(ec) &&
                     (fs::exists(e.path() / "skin.json", ec) || fs::exists(e.path() / "main.bmp", ec) ||
                      fs::exists(e.path() / "MAIN.BMP", ec)))
                out.push_back(e.path().u8string());
        }
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return fs::u8path(a).stem().u8string() < fs::u8path(b).stem().u8string(); });
    return out;
}

bool UIManager::loadSkin(const std::string& path) {
    std::string err;
    auto s = Skin::load(path, err);
    if (!s) { skin_error_ = err; return false; }
    Fonts::noteText(s->name);
    if (skin_) skin_->freeTextures();
    skin_ = std::move(s);
    skin_page_ = skin_->start_page;
    skin_prev_zoom_ = 0.f;
    // A newly picked skin opens at its preferred zoom, but never bigger than 75%
    // of the screen (big skins like MidnightNeon would fill it). Reopening the
    // saved skin keeps the zoom chosen for it.
    if (path != skin_path_ || skin_zoom_ <= 0.f) {
        const float want = skin_->default_zoom > 0.f ? skin_->default_zoom : 2.f;
        skin_zoom_ = std::min(want, skinFitZoom(0.75f));
    }
    skin_error_.clear();
    skin_path_ = path;
    if (skin_mode_) applySkinWindow();   // reshape the window for the new skin
    saveSettings();
    return true;
}

void UIManager::openSkinFile() {
#ifdef _WIN32
    char buf[4096] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Skins (*.wsz;*.zip;*.paskin;skin.json)\0*.wsz;*.zip;*.paskin;skin.json\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrTitle  = "Load a skin";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) loadSkin(buf);
#else
    FILE* f = popen("zenity --file-selection --title='Load a skin' "
                    "--file-filter='Skins|*.wsz *.zip *.paskin skin.json' 2>/dev/null", "r");
    if (!f) return;
    char buf[4096];
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (buf[0]) loadSkin(buf);
    }
    pclose(f);
#endif
}

// ─── Entering / leaving classic mode ──────────────────────────────────────────
SDL_HitTestResult UIManager::skinHitTest(SDL_Window*, const SDL_Point* pt, void* data) {
    auto* self = static_cast<UIManager*>(data);
    if (!self->skin_mode_ || !self->skin_ || self->skin_popup_open_) return SDL_HITTEST_NORMAL;
    const float z = self->skin_zoom_;
    const float x = pt->x / z, y = pt->y / z;
    for (auto& r : self->skin_hot_)
        if (x >= r.x && x < r.x + r.z && y >= r.y && y < r.y + r.w) return SDL_HITTEST_NORMAL;
    return SDL_HITTEST_DRAGGABLE;   // everything else moves the window
}

// Zoom at which the skin takes `fraction` of the usable screen (1 if unknown)
float UIManager::skinFitZoom(float fraction) const {
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    SDL_Rect usable;
    if (!win || !skin_ || skin_->width <= 0 || skin_->height <= 0 ||
        SDL_GetDisplayUsableBounds(std::max(0, SDL_GetWindowDisplayIndex(win)), &usable) != 0)
        return 1.f;
    return std::min(usable.w * fraction / skin_->width, usable.h * fraction / skin_->height);
}

void UIManager::applySkinWindow() {
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    if (!win || !skin_) return;
    skin_->uploadTextures();
    // Never bigger than the screen the window is on
    skin_zoom_ = std::min(skin_zoom_, skinFitZoom(0.97f));
    const int w = (int)std::lround(skin_->width * skin_zoom_);
    const int h = (int)std::lround(skin_->height * skin_zoom_);
    SDL_SetWindowMinimumSize(win, 1, 1);
    SDL_SetWindowSize(win, w, h);
    setWindowShape(win, &skin_->mask, skin_->width, skin_->height, skin_zoom_);
}

void UIManager::toggleSkinMode() {
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    if (!win) return;

    if (!skin_mode_) {
        if (!skin_) {
            // First time: the saved skin, else the first one installed
            auto list = findSkins();
            if (!(!skin_path_.empty() && loadSkin(skin_path_)) && !(!list.empty() && loadSkin(list[0]))) {
                skin_error_ = "No skins found. Put .wsz (Winamp) or PulseAmp skins in the skins folder.";
                show_sidebar_ = true; sidebar_tab_ = 3;
                return;
            }
        }
        if (fullscreen_) toggleFullscreen();
        Uint32 flags = SDL_GetWindowFlags(win);
        saved_maximized_ = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (saved_maximized_) SDL_RestoreWindow(win);
        SDL_GetWindowPosition(win, &saved_x_, &saved_y_);
        SDL_GetWindowSize(win, &saved_w_, &saved_h_);

        skin_mode_ = true;
        SDL_SetWindowBordered(win, SDL_FALSE);
        SDL_SetWindowResizable(win, SDL_FALSE);
        applySkinWindow();
        SDL_SetWindowHitTest(win, &UIManager::skinHitTest, this);
    } else {
        skin_mode_ = false;
        SDL_SetWindowHitTest(win, nullptr, nullptr);
        setWindowShape(win, nullptr, 0, 0, 1.f);
        SDL_SetWindowAlwaysOnTop(win, SDL_FALSE);
        SDL_SetWindowBordered(win, SDL_TRUE);
        SDL_SetWindowResizable(win, SDL_TRUE);
        SDL_SetWindowMinimumSize(win, 480, 320);
        if (saved_w_ > 0) {
            SDL_SetWindowSize(win, saved_w_, saved_h_);
            SDL_SetWindowPosition(win, saved_x_, saved_y_);
        }
        if (saved_maximized_) SDL_MaximizeWindow(win);
    }
    saveSettings();
}

void UIManager::setSkinZoom(float z) {
    skin_zoom_ = std::clamp(z, 0.3f, 100.f);   // applySkinWindow() limits it to the screen
    if (skin_mode_) applySkinWindow();
    saveSettings();
}

// ─── Actions from skin buttons ────────────────────────────────────────────────
void UIManager::skinAction(const std::string& a) {
    if (a.rfind("page:", 0) == 0)  { skin_page_ = a.substr(5); return; }
    if (a.rfind("list:", 0) == 0)  {
        skin_list_ = a.substr(5);
        if (skin_list_ == "library") startLibraryScan();
        return;
    }
    if (a == "play")           { if (player_.getState() != PlayerState::Playing) togglePlay(); }
    else if (a == "pause")     player_.pause();
    else if (a == "playpause") togglePlay();
    else if (a == "stop")      player_.stop();
    else if (a == "next")      { if (playlist_.next()) playEntry(playlist_.getIndex()); }
    else if (a == "prev")      { if (playlist_.prev()) playEntry(playlist_.getIndex()); }
    else if (a == "open" || a == "add") openFile();
    else if (a == "menu" || a == "more") ImGui::OpenPopup("##skinmenu");
    else if (a == "close")     quit_requested_ = true;
    else if (a == "minimize")  { if (SDL_Window* w = SDL_GL_GetCurrentWindow()) SDL_MinimizeWindow(w); }
    else if (a == "maximize") {
        // Toggle between the skin filling the screen and the previous size
        if (skin_prev_zoom_ > 0.f) { float z = skin_prev_zoom_; skin_prev_zoom_ = 0.f; setSkinZoom(z); }
        else { skin_prev_zoom_ = skin_zoom_; setSkinZoom(100.f); }   // clamped to the screen
    }
    else if (a == "fullwindow") toggleSkinMode();
    else if (a == "playlist")  { toggleSkinMode(); show_sidebar_ = true; sidebar_tab_ = 0; }
    else if (a == "settings")  { toggleSkinMode(); show_sidebar_ = true; sidebar_tab_ = 3; }
    else if (a == "fullscreen") { toggleSkinMode(); toggleFullscreen(); }
    else if (a == "mute")      player_.setMuted(!player_.isMuted());
    else if (a == "shuffle")   playlist_.setShuffle(!playlist_.getShuffle());
    else if (a == "repeat") {
        RepeatMode r = playlist_.getRepeat();
        playlist_.setRepeat(r == RepeatMode::None ? RepeatMode::All : r == RepeatMode::All ? RepeatMode::One : RepeatMode::None);
    }
    else if (a == "favorite")  { if (const PlaylistEntry* pe = playlist_.current()) toggleFavorite(*pe); }
    else if (a == "eq_toggle") { eq_.setEnabled(!eq_.enabled()); saveSettings(); }
    else if (a == "preset_next") milkdrop_.next();
    else if (a == "preset_prev") milkdrop_.previous();
    else if (a == "viz_next")  viz_.setMode((Visualizer::Mode)(((int)viz_.getMode() + 1) % vizModeCount()));
}

bool UIManager::skinToggleOn(const std::string& a) const {
    if (a.rfind("page:", 0) == 0) return skin_page_ == a.substr(5);
    if (a.rfind("list:", 0) == 0) return skin_list_ == a.substr(5);
    if (a == "playpause" || a == "play") return player_.getState() == PlayerState::Playing;
    if (a == "pause")   return player_.getState() == PlayerState::Paused;
    if (a == "mute")    return player_.isMuted();
    if (a == "shuffle") return playlist_.getShuffle();
    if (a == "repeat")  return playlist_.getRepeat() != RepeatMode::None;
    if (a == "favorite") { const PlaylistEntry* pe = playlist_.current(); return pe && isFavorite(pe->path); }
    if (a == "eq_toggle") return eq_.enabled();
    return false;
}

std::string UIManager::skinText(const std::string& what, const std::string& custom) const {
    const PlaylistEntry* pe = playlist_.current();
    const MediaInfo& mi = player_.mediaInfo();
    const bool has = pe && player_.getFilePath() == pe->path;   // info belongs to this entry
    const double cur = player_.getCurrentTime(), dur = player_.getDuration();
    if (what == "title") {
        if (open_job_.valid()) return "Loading...";
        if (has && !mi.title.empty()) return mi.title;
        return pe ? pe->title : "PulseAmp";
    }
    if (what == "artist") {
        if (has && !mi.artist.empty()) return mi.artist;
        return pe ? pe->artist : "";
    }
    if (what == "album")     return has ? mi.album : "";
    if (what == "format") {
        if (!has || mi.codec.empty()) return "";
        std::string s = mi.codec;
        char b[32];
        if (mi.sample_rate) { std::snprintf(b, sizeof(b), "   |   %.1f kHz", mi.sample_rate / 1000.0); s += b; }
        if (mi.bits)        { std::snprintf(b, sizeof(b), "   |   %d bit", mi.bits); s += b; }
        if (mi.bitrate_kbps){ std::snprintf(b, sizeof(b), "   |   %d kbps", mi.bitrate_kbps); s += b; }
        return s;
    }
    if (what == "time")      return formatTime(cur);
    if (what == "remaining") return "-" + formatTime(std::max(0.0, dur - cur));
    if (what == "duration")  return formatTime(dur);
    if (what == "time_total") return formatTime(cur) + " / " + formatTime(dur);
    if (what == "bpm")       { char b[32]; std::snprintf(b, sizeof(b), "%.0f BPM", displayed_bpm_); return displayed_bpm_ > 0 ? b : ""; }
    if (what == "preset")    return milkdrop_.presetName();
    if (what == "volume")    { char b[32]; std::snprintf(b, sizeof(b), "%.0f%%", player_.getVolume() * 100.f); return b; }
    if (what == "page")      return skin_page_;
    return custom;
}

// ─── Frame ────────────────────────────────────────────────────────────────────
void UIManager::drawSkinMode(int win_w, int win_h, double time) {
    skin_hot_.clear();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)win_w, (float)win_h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##skin", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollWithMouse);

    skin_o_ = ImGui::GetWindowPos();
    if (skin_->kind == Skin::Kind::Winamp) drawWinampSkin(time);
    else                                   drawPulseSkin(time);

    // Right-click on a control, or the skin's menu button: the classic-mode menu
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##skinmenu");
    skin_popup_open_ = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);   // menus, dropdowns
    // Mouse wheel: volume (track lists are child windows and scroll themselves)
    if (ImGui::GetIO().MouseWheel != 0.f && !skin_popup_open_ && ImGui::IsWindowHovered())
        setVolume(volume_ + ImGui::GetIO().MouseWheel * 0.05f);
    if (ImGui::BeginPopup("##skinmenu")) {
        drawSkinMenu();
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

void UIManager::drawSkinMenu() {
    if (ImGui::MenuItem("Back to full window", "Ctrl+M")) toggleSkinMode();
    if (ImGui::MenuItem("Open file...", "O")) openFile();
    ImGui::Separator();
    if (ImGui::BeginMenu("Visualizer")) {
        drawVizMenu();
        ImGui::EndMenu();
    }
    drawSkinList();
    if (ImGui::BeginMenu("Zoom")) {
        for (float z : { 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f }) {
            char label[16];
            std::snprintf(label, sizeof(label), "%gx", z);
            if (ImGui::MenuItem(label, nullptr, std::fabs(skin_zoom_ - z) < 0.01f)) setSkinZoom(z);
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Always on top", nullptr, always_on_top_)) {
        always_on_top_ = !always_on_top_;
        if (SDL_Window* w = SDL_GL_GetCurrentWindow())
            SDL_SetWindowAlwaysOnTop(w, always_on_top_ ? SDL_TRUE : SDL_FALSE);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Esc")) quit_requested_ = true;
}

// Skins submenu, shared by the menu bar and the classic-mode menu
void UIManager::drawSkinList() {
    if (!ImGui::BeginMenu("Skins")) return;
    auto list = findSkins();
    if (list.empty()) ImGui::TextDisabled("No skins installed");
    for (auto& p : list) {
        std::string name = fs::u8path(p).stem().u8string();
        Fonts::noteText(name);
        if (ImGui::MenuItem(name.c_str(), nullptr, p == skin_path_)) {
            loadSkin(p);
            if (!skin_mode_) toggleSkinMode();
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Load skin file...")) { openSkinFile(); if (skin_ && !skin_mode_) toggleSkinMode(); }
    if (ImGui::MenuItem("Open skins folder")) {
        std::string dir = (fs::path(appDataDir()) / "skins").u8string();
        std::string url = "file:///" + dir;
        std::replace(url.begin(), url.end(), '\\', '/');
        SDL_OpenURL(url.c_str());
    }
    if (ImGui::MenuItem("Get Winamp skins (skins.webamp.org)"))
        SDL_OpenURL("https://skins.webamp.org/");
    ImGui::EndMenu();
}

// Interactive region: an invisible button at skin coordinates; also marks it
// as "not draggable" for the window hit test
bool UIManager::skinHot(const char* id, float x, float y, float w, float h, bool* pressed) {
    const float z = skin_zoom_;
    ImGui::SetCursorScreenPos({skin_o_.x + x * z, skin_o_.y + y * z});
    bool clicked = ImGui::InvisibleButton(id, {std::max(1.f, w * z), std::max(1.f, h * z)});
    skin_hot_.push_back({x, y, w, h});
    if (pressed) *pressed = ImGui::IsItemActive() && ImGui::IsItemHovered();
    return clicked;
}

// ─── Winamp 2.x classic skin ──────────────────────────────────────────────────
void UIManager::drawWinampSkin(double time) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float z = skin_zoom_;
    const ImVec2 o = skin_o_;
    auto blit = [&](const char* key, float sx, float sy, float w, float h, float dx, float dy) {
        const SkinImage* img = skin_->image(key);
        if (!img || !img->tex || sx + w > img->w || sy + h > img->h) return;
        dl->AddImage(texId(img), {o.x + dx * z, o.y + dy * z}, {o.x + (dx + w) * z, o.y + (dy + h) * z},
                     {sx / img->w, sy / img->h}, {(sx + w) / img->w, (sy + h) / img->h});
    };
    auto text = [&](const std::string& s, float dx, float dy, float max_w, float offset_px = 0.f) {
        const SkinImage* img = skin_->image("text");
        if (!img || !img->tex) return;
        dl->PushClipRect({o.x + dx * z, o.y + dy * z}, {o.x + (dx + max_w) * z, o.y + (dy + 6) * z}, true);
        float x = dx - offset_px;
        for (unsigned char c : s) {
            int col, row;
            winampGlyph(c, col, row);
            if (x + 5 > dx && x < dx + max_w) blit("text", col * 5.f, row * 6.f, 5, 6, x, dy);
            x += 5;
        }
        dl->PopClipRect();
    };

    const auto state = player_.getState();
    const double cur = player_.getCurrentTime(), dur = player_.getDuration();
    bool p;

    blit("main", 0, 0, 275, 116, 0, 0);
    const bool focused = SDL_GetWindowFlags(SDL_GL_GetCurrentWindow()) & SDL_WINDOW_INPUT_FOCUS;
    blit("titlebar", 27, focused ? 0 : 15, 275, 14, 0, 0);

    // Title bar buttons: options (menu), minimize, "shade" (back to full window), close
    if (skinHot("##w_opt", 6, 3, 9, 9, &p))    skinAction("menu");
    blit("titlebar", 0, p ? 9 : 0, 9, 9, 6, 3);
    if (skinHot("##w_min", 244, 3, 9, 9, &p))  skinAction("minimize");
    blit("titlebar", 9, p ? 9 : 0, 9, 9, 244, 3);
    if (skinHot("##w_full", 254, 3, 9, 9, &p)) skinAction("fullwindow");
    blit("titlebar", p ? 9 : 0, 18, 9, 9, 254, 3);
    if (skinHot("##w_close", 264, 3, 9, 9, &p)) skinAction("close");
    blit("titlebar", 18, p ? 9 : 0, 9, 9, 264, 3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close PulseAmp");

    // Transport buttons (cbuttons.bmp)
    struct Btn { const char* id; const char* action; float sx, w, h, dx, dy, pressed_y; };
    static const Btn btns[] = {
        {"##w_prev", "prev",       0, 23, 18,  16, 88, 18}, {"##w_play", "play",   23, 23, 18,  39, 88, 18},
        {"##w_pause","pause",     46, 23, 18,  62, 88, 18}, {"##w_stop", "stop",   69, 23, 18,  85, 88, 18},
        {"##w_next", "next",      92, 22, 18, 108, 88, 18}, {"##w_eject","open",  114, 22, 16, 136, 89, 16} };
    for (auto& b : btns) {
        if (skinHot(b.id, b.dx, b.dy, b.w, b.h, &p)) skinAction(b.action);
        blit("cbuttons", b.sx, p ? b.pressed_y : 0, b.w, b.h, b.dx, b.dy);
    }

    // Play state indicator and time (numbers.bmp)
    blit("playpaus", state == PlayerState::Playing ? 0.f : state == PlayerState::Paused ? 9.f : 18.f, 0, 9, 9, 26, 28);
    if (state != PlayerState::Stopped) {
        int secs = (int)cur;
        int m = std::min(99, secs / 60), s = secs % 60;
        const int digits[4] = { m / 10, m % 10, s / 10, s % 10 };
        const float xs[4] = { 48, 60, 78, 90 };
        for (int i = 0; i < 4; ++i) blit("numbers", digits[i] * 9.f, 0, 9, 13, xs[i], 26);
    }

    // Scrolling title
    std::string title;
    if (const PlaylistEntry* pe = playlist_.current()) {
        char b[64];
        std::snprintf(b, sizeof(b), "%d. ", playlist_.getIndex() + 1);
        title = b + (open_job_.valid() ? std::string("Loading...") : pe->title);
        if (pe->duration > 0 || dur > 0) title += " (" + formatTime(dur > 0 ? dur : pe->duration) + ")";
    } else {
        title = "PulseAmp " PULSEAMP_VERSION " - drop a file here";
    }
    if (title.size() * 5 > 154) {
        title += "  ***  ";
        float len_px = title.size() * 5.f;
        text(title + title, 111, 27, 154, std::fmod((float)time * 25.f, len_px));
    } else {
        text(title, 111, 27, 154);
    }
    text(state != PlayerState::Stopped ? "44" : "", 156, 43, 10);   // kHz (output rate)

    // Mono / stereo
    const bool stereo = state != PlayerState::Stopped && player_.hasAudio();
    blit("monoster", 29, 12, 27, 12, 212, 41);
    blit("monoster", 0, stereo ? 0.f : 12.f, 29, 12, 239, 41);

    // Seek bar (posbar.bmp): seek when released, like Winamp
    blit("posbar", 0, 0, 248, 10, 16, 72);
    skinHot("##w_seek", 16, 72, 248, 10, &p);
    float frac = dur > 0 ? (float)(cur / dur) : 0.f;
    if (ImGui::IsItemActive() && dur > 0) {
        float mx = (ImGui::GetIO().MousePos.x - o.x) / z;
        skin_seek_preview_ = std::clamp((mx - 16.f - 14.5f) / (248.f - 29.f), 0.f, 1.f);
        frac = skin_seek_preview_;
    }
    if (ImGui::IsItemDeactivated() && dur > 0) player_.seek(skin_seek_preview_ * dur);
    if (dur > 0) blit("posbar", ImGui::IsItemActive() ? 278.f : 248.f, 0, 29, 10, 16 + frac * (248 - 29), 72);

    // Volume (volume.bmp: 28 background frames + thumb)
    float vol = std::min(1.f, player_.getVolume());   // the bar shows 0..100% (boost shows full)
    skinHot("##w_vol", 107, 57, 68, 13, &p);
    if (ImGui::IsItemActive()) {
        float mx = (ImGui::GetIO().MousePos.x - o.x) / z;
        vol = std::clamp((mx - 107.f - 7.f) / (68.f - 14.f), 0.f, 1.f);
        setVolume(vol);
    }
    blit("volume", 0, std::round(vol * 27.f) * 15.f, 68, 13, 107, 57);
    blit("volume", ImGui::IsItemActive() ? 0.f : 15.f, 422, 14, 11, 107 + vol * (68 - 14), 58);

    // Balance (display only: centred)
    skinHot("##w_bal", 177, 57, 38, 13, nullptr);
    blit("balance", 9, 0, 38, 13, 177, 57);
    blit("balance", 15, 422, 14, 11, 177 + 12, 58);

    // Shuffle / repeat, EQ (-> settings), PL (-> playlist)
    auto toggle = [&](const char* id, const char* action, bool on, float sx, float w, float h,
                      float dx, float dy, float y_off, float y_on, float y_off_p, float y_on_p) {
        if (skinHot(id, dx, dy, w, h, &p)) skinAction(action);
        blit("shufrep", sx, on ? (p ? y_on_p : y_on) : (p ? y_off_p : y_off), w, h, dx, dy);
    };
    toggle("##w_rep",  "repeat",  skinToggleOn("repeat"),  0, 28, 15, 210, 89, 0, 30, 15, 45);
    toggle("##w_shuf", "shuffle", skinToggleOn("shuffle"), 28, 47, 15, 164, 89, 0, 30, 15, 45);
    if (skinHot("##w_eq", 219, 58, 23, 12, &p)) skinAction("settings");
    blit("shufrep", p ? 46.f : 0.f, 61, 23, 12, 219, 58);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings (full window)");
    if (skinHot("##w_pl", 242, 58, 23, 12, &p)) skinAction("playlist");
    blit("shufrep", p ? 69.f : 23.f, 61, 23, 12, 242, 58);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playlist (full window)");

    // Classic visualizer (click: spectrum -> oscilloscope -> off)
    if (skinHot("##w_vis", 24, 43, 76, 16, nullptr)) winamp_vis_ = (winamp_vis_ + 1) % 3;
    const ImU32* vc = skin_->viscolors;
    auto px = [&](float x, float y, float w, float h, ImU32 c) {
        dl->AddRectFilled({o.x + x * z, o.y + y * z}, {o.x + (x + w) * z, o.y + (y + h) * z}, c);
    };
    const bool live = state == PlayerState::Playing;
    if (winamp_vis_ == 0 && live) {
        const float* bars = viz_.bars();
        const float* peaks = viz_.barPeaks();
        const int nb = Visualizer::barCount();
        for (int i = 0; i < 19; ++i) {
            int a = i * nb / 19, b = std::max(a + 1, (i + 1) * nb / 19);
            float v = 0.f, pk = 0.f;
            for (int k = a; k < b; ++k) { v = std::max(v, bars[k]); pk = std::max(pk, peaks[k]); }
            int hbar = std::clamp((int)std::round(v * 16.f), 0, 16);
            for (int r = 16 - hbar; r < 16; ++r) px(24.f + i * 4, 43.f + r, 3, 1, vc[2 + r]);
            int prow = std::clamp(16 - (int)std::round(pk * 16.f), 0, 15);
            if (pk > 0.02f) px(24.f + i * 4, 43.f + prow, 3, 1, vc[23]);
        }
    } else if (winamp_vis_ == 1 && live) {
        const float* wv = viz_.wave();
        for (int x = 0; x < 76; ++x) {
            float s = wv[x * Visualizer::waveCount() / 2 / 76];
            int row = std::clamp((int)std::round(8.f - s * 8.f), 0, 15);
            px(24.f + x, 43.f + row, 1, 1, vc[18 + std::min(4, std::abs(row - 8) / 2)]);
        }
    }
}

