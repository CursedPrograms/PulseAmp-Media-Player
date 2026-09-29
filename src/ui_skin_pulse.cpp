// ─── ui_skin_pulse.cpp ────────────────────────────────────────────────────────
// PulseAmp (skin.json) skins: panels, lists with cover art, the equalizer,
// search, pages and neon glow. Also favorites and the music library.
// Part of UIManager (see ui_manager.h).
// ─────────────────────────────────────────────────────────────────────────────
#include "ui_manager.h"
#include "skin_draw.h"
#include "fonts.h"
#include "logos.h"
#include "paths.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace SkinDraw;

namespace {

ImTextureID tex(unsigned t) { return (ImTextureID)(intptr_t)t; }

// Pink -> cyan -> green, as in the Neon Wave visualizer
ImU32 neon(float t, float alpha = 1.f) {
    const float p[3] = {255, 60, 200}, c[3] = {1, 220, 255}, g[3] = {0, 255, 140};
    const float* a = t < 0.5f ? p : c;
    const float* b = t < 0.5f ? c : g;
    float k = t < 0.5f ? t * 2.f : (t - 0.5f) * 2.f;
    return IM_COL32((int)(a[0] + (b[0] - a[0]) * k), (int)(a[1] + (b[1] - a[1]) * k),
                    (int)(a[2] + (b[2] - a[2]) * k), (int)(255 * alpha));
}

// UVs that crop an image to fill a box (like CSS object-fit: cover)
void coverUV(int iw, int ih, float bw, float bh, ImVec2& uv0, ImVec2& uv1) {
    uv0 = {0, 0}; uv1 = {1, 1};
    if (iw <= 0 || ih <= 0 || bw <= 0 || bh <= 0) return;
    float ia = (float)iw / ih, ba = bw / bh;
    if (ia > ba) { float k = ba / ia; uv0.x = (1 - k) * 0.5f; uv1.x = 1 - uv0.x; }
    else         { float k = ia / ba; uv0.y = (1 - k) * 0.5f; uv1.y = 1 - uv0.y; }
}

bool isAudioVideoFile(const fs::path& p) {
    static const char* exts[] = { ".mp3", ".flac", ".wav", ".ogg", ".opus", ".m4a", ".aac", ".wma",
                                  ".mp4", ".mkv", ".webm", ".mov", ".avi" };
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    for (auto x : exts) if (e == x) return true;
    return false;
}

} // namespace

// ─── Cover art for what's playing ─────────────────────────────────────────────
const MediaCache::Info* UIManager::currentArt() {
    const PlaylistEntry* pe = playlist_.current();
    if (!pe) return nullptr;
    const MediaInfo& mi = player_.mediaInfo();
    if (player_.getFilePath() == pe->path && !mi.art.empty()) return media_.fromBytes(pe->path, mi.art);
    if (!pe->art.empty()) return media_.get(pe->art);
    if (isUrl(pe->path)) { std::string t = youtubeThumbnail(pe->path); return t.empty() ? nullptr : media_.get(t); }
    return media_.get(pe->path);   // folder art (cover.jpg) or tags
}

// ─── Favorites (<appdata>/favorites.txt) ──────────────────────────────────────
bool UIManager::isFavorite(const std::string& path) const {
    return std::any_of(favorites_.begin(), favorites_.end(), [&](const PlaylistEntry& e) { return e.path == path; });
}

void UIManager::toggleFavorite(const PlaylistEntry& e) {
    auto it = std::find_if(favorites_.begin(), favorites_.end(), [&](const PlaylistEntry& f) { return f.path == e.path; });
    if (it != favorites_.end()) favorites_.erase(it);
    else {
        PlaylistEntry f = e;
        const MediaInfo& mi = player_.mediaInfo();
        if (player_.getFilePath() == e.path) {
            if (!mi.title.empty()) f.title = mi.title;
            if (!mi.artist.empty()) f.artist = mi.artist;
            if (f.duration <= 0) f.duration = player_.getDuration();
        }
        favorites_.push_back(f);
    }
    saveFavorites();
}

void UIManager::loadFavorites() {
    std::ifstream f(appDataPath("favorites.txt"));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> v;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '\t')) v.push_back(part);
        if (v.empty() || v[0].empty()) continue;
        PlaylistEntry e;
        e.path = v[0];
        if (v.size() > 1) e.title = v[1];
        if (v.size() > 2) e.artist = v[2];
        if (v.size() > 3) e.duration = std::atof(v[3].c_str());
        if (v.size() > 4) e.art = v[4];
        if (e.title.empty()) e.title = fs::u8path(e.path).stem().u8string();
        favorites_.push_back(e);
    }
}

void UIManager::saveFavorites() const {
    std::ofstream f(appDataPath("favorites.txt"));
    for (auto& e : favorites_)
        f << e.path << '\t' << e.title << '\t' << e.artist << '\t' << e.duration << '\t' << e.art << '\n';
}

// ─── Library: audio/video files in the user's Music folder ────────────────────
void UIManager::startLibraryScan() {
    if (library_started_) return;
    library_started_ = true;
    library_scanning_ = true;
    library_th_ = std::thread([this] {
        std::vector<PlaylistEntry> found;
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home) {
            std::error_code ec;
            fs::path music = fs::path(home) / "Music";
            for (auto it = fs::recursive_directory_iterator(music, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!library_scanning_) break;                       // shutting down
                if (it->is_regular_file(ec) && isAudioVideoFile(it->path())) {
                    PlaylistEntry e;
                    e.path = it->path().u8string();
                    e.title = it->path().stem().u8string();
                    found.push_back(e);
                    if (found.size() >= 20000) break;
                }
            }
        }
        std::sort(found.begin(), found.end(), [](const PlaylistEntry& a, const PlaylistEntry& b) { return a.title < b.title; });
        std::lock_guard lk(library_mu_);
        library_pending_ = std::move(found);
        library_pending_ready_ = true;
        library_scanning_ = false;
    });
}

// ─── GL cleanup ───────────────────────────────────────────────────────────────
void UIManager::releaseGL() {
    library_scanning_ = false;
    if (library_th_.joinable()) library_th_.join();
    if (skin_) skin_->freeTextures();
    milkdrop_.shutdown();
    media_.releaseGL();
    Logos::release();
}

// ─── The skin ─────────────────────────────────────────────────────────────────
void UIManager::drawPulseSkin(double time) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float z = skin_zoom_;
    const ImVec2 o = skin_o_;
    const Skin& sk = *skin_;
    auto S2 = [&](float x, float y) { return ImVec2(o.x + x * z, o.y + y * z); };

    // Background
    const ImVec2 b0 = S2(0, 0), b1 = S2((float)sk.width, (float)sk.height);
    const float bg_round = sk.shape == "rounded" ? sk.corner_radius * z : 0.f;
    if (const SkinImage* bg = sk.image(sk.background)) {
        dl->AddImageRounded((ImTextureID)(intptr_t)bg->tex, b0, b1, {0, 0}, {1, 1}, IM_COL32_WHITE, bg_round);
    } else if (sk.shape == "circle" || sk.shape == "ellipse") {
        dl->AddCircleFilled({(b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f}, (b1.x - b0.x) * 0.5f, sk.bg_color, 96);
    } else {
        dl->AddRectFilled(b0, b1, sk.bg_color, bg_round);
    }

    for (size_t i = 0; i < sk.controls.size(); ++i) {
        const SkinControl& c = sk.controls[i];
        if (!c.pages.empty() && std::find(c.pages.begin(), c.pages.end(), skin_page_) == c.pages.end()) continue;

        const ImVec2 p0 = S2(c.x, c.y), p1 = S2(c.x + c.w, c.y + c.h);
        const ImVec2 mid = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
        const float  rnd = c.shape == "rect" ? 0.f : c.shape == "circle" ? std::min(p1.x - p0.x, p1.y - p0.y) * 0.5f : c.radius * z;
        char id[32];
        std::snprintf(id, sizeof(id), "##sc%zu", i);

        auto shapeFill = [&](ImU32 col) {
            if (!col) return;
            if (c.shape == "circle") dl->AddCircleFilled(mid, std::min(p1.x - p0.x, p1.y - p0.y) * 0.5f, col, 48);
            else dl->AddRectFilled(p0, p1, col, rnd);
        };
        auto shapeBorder = [&](ImU32 col) {
            if (!col) return;
            if (c.shape == "circle") dl->AddCircle(mid, std::min(p1.x - p0.x, p1.y - p0.y) * 0.5f, col, 48, c.border_width * z);
            else dl->AddRect(p0, p1, col, rnd, 0, c.border_width * z);
        };
        auto shapeGlow = [&](ImU32 col) {
            if (!col) return;
            if (c.shape == "circle") glowCircle(dl, mid, std::min(p1.x - p0.x, p1.y - p0.y) * 0.5f, col, c.glow_size * z);
            else glowRect(dl, p0, p1, rnd, col, c.glow_size * z);
        };
        auto drawImg = [&](const std::string& key) {
            if (const SkinImage* img = sk.image(key)) { dl->AddImageRounded((ImTextureID)(intptr_t)img->tex, p0, p1, {0, 0}, {1, 1}, IM_COL32_WHITE, rnd); return true; }
            return false;
        };

        if (c.type == "panel") {
            shapeGlow(c.glow);
            shapeFill(c.bg);
            shapeBorder(c.border);
        } else if (c.type == "image") {
            drawImg(c.image);
        } else if (c.type == "text") {
            std::string s = skinText(c.content.empty() ? "custom" : c.content, c.text);
            if (s.empty()) continue;
            Fonts::noteText(s);
            const float fs = c.font_size * z;
            ImFont* font = fontFor(fs);
            ImVec2 ts = textSize(font, fs, s);
            float x = p0.x, y = mid.y - ts.y * 0.5f;
            dl->PushClipRect({p0.x - c.glow_size * z, p0.y - c.glow_size * z}, {p1.x + c.glow_size * z, p1.y + c.glow_size * z}, true);
            if (ts.x > p1.x - p0.x && c.scroll) {
                std::string loop = s + "     ";
                float lw = textSize(font, fs, loop).x;
                float off = std::fmod((float)time * 30.f * z, lw);
                dl->PushClipRect(p0, p1, true);
                text(dl, font, fs, {x - off, y}, c.color, loop, c.glow);
                text(dl, font, fs, {x - off + lw, y}, c.color, loop, c.glow);
                dl->PopClipRect();
            } else {
                if (c.align == "center") x = mid.x - ts.x * 0.5f;
                else if (c.align == "right") x = p1.x - ts.x;
                text(dl, font, fs, {x, y}, c.color, s, c.glow);
            }
            dl->PopClipRect();
        } else if (c.type == "art") {
            shapeGlow(c.glow);
            const MediaCache::Info* info = currentArt();
            unsigned t = info && info->tex ? info->tex : media_.placeholder();
            ImVec2 uv0, uv1;
            coverUV(info && info->tex ? info->w : 1, info && info->tex ? info->h : 1, p1.x - p0.x, p1.y - p0.y, uv0, uv1);
            dl->AddImageRounded(tex(t), p0, p1, uv0, uv1, IM_COL32_WHITE, rnd);
            shapeBorder(c.border);
        } else if (c.type == "visualizer") {
            shapeGlow(c.glow);
            shapeFill(c.bg);
            bool clicked = skinHot(id, c.x, c.y, c.w, c.h, nullptr);
            if (c.video && player_.hasVideo() && video_.isReady()) {
                // A video is playing: show it here (letterboxed) instead of the visualizer
                dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255), rnd);
                const float bw = p1.x - p0.x, bh = p1.y - p0.y;
                const float ar = (float)video_.getWidth() / std::max(1, video_.getHeight());
                const float dw = std::min(bw, bh * ar), dh = dw / ar;
                const ImVec2 q0 = {mid.x - dw * 0.5f, mid.y - dh * 0.5f}, q1 = {mid.x + dw * 0.5f, mid.y + dh * 0.5f};
                dl->AddImageRounded(video_.getTextureID(), q0, q1, {0, 0}, {1, 1}, IM_COL32_WHITE, rnd);
                if (clicked) togglePlay();
                shapeBorder(c.border);
                continue;
            }
            Visualizer::Mode m = viz_.getMode();
            static const std::pair<const char*, Visualizer::Mode> modes[] = {
                {"spectrum", Visualizer::Mode::SpectrumBars}, {"oscilloscope", Visualizer::Mode::Oscilloscope},
                {"radial", Visualizer::Mode::RadialSpectrum}, {"bpm", Visualizer::Mode::BPMPulse},
                {"particles", Visualizer::Mode::ParticleStorm}, {"neon", Visualizer::Mode::NeonWave},
                {"milkdrop", Visualizer::Mode::MilkDrop} };
            for (auto& [n, mm] : modes) if (c.viz_mode == n) m = mm;
            if (clicked && c.viz_mode == "current") skinAction("viz_next");
            const float pad = c.bg ? 10.f * z : 0.f;
            const ImVec2 v0 = {p0.x + pad, p0.y + pad}, v1 = {p1.x - pad, p1.y - pad};
            dl->PushClipRect(p0, p1, true);
            if (m == Visualizer::Mode::MilkDrop) {
                if (milkdrop_.init() && milkdrop_.presetCount() > 0) {
                    milkdrop_w_ = p1.x - p0.x;          // rendered in preRenderGL()
                    milkdrop_h_ = p1.y - p0.y;
                    if (milkdrop_.texture())
                        dl->AddImageRounded(tex(milkdrop_.texture()), p0, p1, {0, 1}, {1, 0}, IM_COL32_WHITE, rnd);
                }
            } else {
                // Draw in the skin's colour and mode, then restore the app's
                Visualizer::Mode saved = viz_.getMode();
                ImU32 saved_col = viz_.getColor();
                viz_.setMode(m);
                viz_.setColor(c.fill);
                viz_.render(dl, v0, {v1.x - v0.x, v1.y - v0.y}, displayed_bpm_, time);
                viz_.setMode(saved);
                viz_.setColor(saved_col);
            }
            dl->PopClipRect();
            shapeBorder(c.border);
        } else if (c.type == "slider") {
            bool pressed;
            skinHot(id, c.x, c.y - 6.f, c.w, c.h + 12.f, &pressed);   // easier to grab thin bars
            const bool active = ImGui::IsItemActive();
            const double dur = player_.getDuration();
            float v = c.action == "volume" ? std::min(1.f, player_.getVolume())   // bar: 0..100%
                    : dur > 0 ? (float)(player_.getCurrentTime() / dur) : 0.f;
            if (active) {
                ImVec2 m = ImGui::GetIO().MousePos;
                v = c.vertical ? 1.f - (m.y - p0.y) / (p1.y - p0.y) : (m.x - p0.x) / (p1.x - p0.x);
                v = std::clamp(v, 0.f, 1.f);
                if (c.action == "volume") setVolume(v);
                else skin_seek_preview_ = v;
            }
            if (active && c.action == "seek") v = skin_seek_preview_;
            if (ImGui::IsItemDeactivated() && c.action == "seek" && dur > 0)
                player_.seek(skin_seek_preview_ * dur);
            if (!drawImg(c.image)) shapeFill(c.bg ? c.bg : IM_COL32(255, 255, 255, 40));
            ImVec2 f1 = c.vertical ? ImVec2(p1.x, p1.y - (p1.y - p0.y) * v) : ImVec2(p0.x + (p1.x - p0.x) * v, p1.y);
            ImVec2 f0 = c.vertical ? ImVec2(p0.x, f1.y) : p0;
            if (c.glow) glowRect(dl, f0, c.vertical ? p1 : f1, rnd, c.glow, c.glow_size * z * 0.6f);
            if (c.vertical) dl->AddRectFilled(f0, p1, c.fill, rnd);
            else            dl->AddRectFilled(f0, f1, c.fill, rnd);
            ImVec2 th = c.vertical ? ImVec2(mid.x, f1.y) : ImVec2(f1.x, mid.y);
            const float tr = (active ? 1.2f : 1.f) * c.radius * z;
            if (const SkinImage* ti = sk.image(c.image_on)) {       // image_on = thumb image
                float tw = ti->w * z * 0.5f, thh = ti->h * z * 0.5f;
                dl->AddImage((ImTextureID)(intptr_t)ti->tex, {th.x - tw, th.y - thh}, {th.x + tw, th.y + thh});
            } else {
                if (c.glow) glowCircle(dl, th, tr, c.glow, c.glow_size * z * 0.8f);
                dl->AddCircleFilled(th, tr, c.color, 24);
            }
        } else if (c.type == "list") {
            drawSkinTrackList(c, (int)i, p0, p1);
        } else if (c.type == "eq") {
            drawSkinEq(c, (int)i, p0, p1);
        } else if (c.type == "search") {
            drawSkinSearch(c, (int)i, p0, p1);
        } else if (c.type == "combo") {
            drawSkinCombo(c, (int)i, p0, p1);
        } else if (c.type == "switch") {
            if (skinHot(id, c.x, c.y, c.w, c.h, nullptr)) skinAction(c.action);
            const bool on = skinToggleOn(c.action);
            const float r = (p1.y - p0.y) * 0.5f;
            if (on && c.glow) glowRect(dl, p0, p1, r, c.glow, c.glow_size * z);
            dl->AddRectFilled(p0, p1, on ? c.fill : (c.bg ? c.bg : IM_COL32(80, 80, 95, 255)), r);
            const float kx = on ? p1.x - r : p0.x + r;
            dl->AddCircleFilled({kx, mid.y}, r * 0.78f, c.color, 24);
        } else {   // button / toggle
            bool pressed;
            if (skinHot(id, c.x, c.y, c.w, c.h, &pressed)) skinAction(c.action);
            const bool hover = ImGui::IsItemHovered();
            const bool on = skinToggleOn(c.action);
            if (hover && !c.text.empty()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            bool drew = false;
            if (!c.image.empty()) {
                drew = (pressed && drawImg(c.image_pressed)) || (on && drawImg(c.image_on)) ||
                       (hover && drawImg(c.image_hover)) || drawImg(c.image);
            }
            if (!drew) {
                if (c.glow && (c.type != "toggle" || on)) shapeGlow(c.glow);
                ImU32 bgc = pressed && c.bg_pressed ? c.bg_pressed : on && c.bg_on ? c.bg_on
                          : hover && c.bg_hover ? c.bg_hover : c.bg;
                shapeFill(bgc);
                shapeBorder(on && c.border_on ? c.border_on : c.border);
                ImU32 fg = pressed && c.color_pressed ? c.color_pressed : on && c.color_on ? c.color_on
                         : hover && c.color_hover ? c.color_hover : c.color;
                std::string icon_name = c.icon;
                if (icon_name.empty() && c.text.empty()) icon_name = c.action;
                if (icon_name == "playpause") icon_name = on ? "pause" : "play";
                if (icon_name == "heart" && on) icon_name = "heart_filled";
                if (icon_name == "logo") {
                    // The PulseAmp logo (wave), fitted in the button, brighter on hover
                    const Logos::Texture& lt = Logos::get(Logos::Wave);
                    if (lt.tex) {
                        const float bw = (p1.x - p0.x) * 0.84f, bh = (p1.y - p0.y) * 0.84f;
                        const float ar = (float)lt.w / std::max(1, lt.h);
                        const float lw = std::min(bw, bh * ar), lh = lw / ar;
                        const ImU32 tint = hover || pressed ? IM_COL32_WHITE : IM_COL32(225, 225, 225, 255);
                        dl->AddImage(tex(lt.tex), {mid.x - lw * 0.5f, mid.y - lh * 0.5f},
                                     {mid.x + lw * 0.5f, mid.y + lh * 0.5f}, {0, 0}, {1, 1}, tint);
                    }
                } else if (!icon_name.empty()) {
                    const float s = std::min(p1.x - p0.x, p1.y - p0.y) * (c.border || bgc ? 0.28f : 0.36f);
                    if (c.glow) glowIcon(dl, icon_name, mid, s, c.glow);
                    icon(dl, icon_name, mid, s, fg);
                }
                if (!c.text.empty()) {
                    const float fs = c.font_size * z;
                    ImFont* font = fontFor(fs);
                    ImVec2 ts = textSize(font, fs, c.text);
                    ImVec2 at = {mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f};
                    if (c.align == "left") at.x = p0.x + 10.f * z;
                    text(dl, font, fs, at, fg, c.text, on ? c.glow : 0);
                    if (c.underline && on)
                        dl->AddLine({at.x, p1.y - 1.5f * z}, {at.x + ts.x, p1.y - 1.5f * z}, c.fill, 2.f * z);
                }
            }
        }
    }
}

// ─── Track list (playlist / library / online / favorites) ─────────────────────
void UIManager::drawSkinTrackList(const SkinControl& c, int idx, ImVec2 p0, ImVec2 p1) {
    const float z = skin_zoom_;
    std::string source = c.source == "left" ? skin_list_ : c.source;
    if (source == "library") startLibraryScan();
    skin_hot_.push_back({c.x, c.y, c.w, c.h});

    // Rows: entries from the source, filtered by the search box (except online)
    struct Row { const PlaylistEntry* e = nullptr; const OnlineResult* r = nullptr; int index = 0; };
    std::vector<Row> rows;
    const std::string filter = source == "online" ? "" : skin_search_;
    auto addEntries = [&](const std::vector<PlaylistEntry>& v) {
        for (int k = 0; k < (int)v.size(); ++k)
            if (containsNoCase(v[k].title, filter) || containsNoCase(v[k].artist, filter) || containsNoCase(v[k].path, filter))
                rows.push_back({&v[k], nullptr, k});
    };
    if (source == "playlist")       addEntries(playlist_.entries());
    else if (source == "library")   addEntries(library_);
    else if (source == "favorites") addEntries(favorites_);
    else if (source == "online")
        for (int k = 0; k < (int)online_results_.size(); ++k) rows.push_back({nullptr, &online_results_[k], k});

    ImDrawList* pdl = ImGui::GetWindowDrawList();
    const char* empty_msg = nullptr;
    if (rows.empty()) {
        empty_msg = source == "playlist" ? "Drop files here, or press O to open"
                  : source == "library" ? (library_scanning_ ? "Scanning your Music folder..." : "No music found in your Music folder")
                  : source == "favorites" ? "Tap the heart on a track to add it here"
                  : online_.searching() ? "Searching..." : "Search YouTube with the search box";
        ImVec2 ts = ImGui::CalcTextSize(empty_msg);
        pdl->AddText({(p0.x + p1.x - ts.x) * 0.5f, p0.y + 30.f * z}, c.color2, empty_msg);
        return;
    }

    ImGui::SetCursorScreenPos(p0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::max(3.f, 5.f * z));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, withAlpha(c.color2, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, withAlpha(c.fill, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, c.fill);
    char cid[32];
    std::snprintf(cid, sizeof(cid), "##list%d", idx);
    ImGui::BeginChild(cid, {p1.x - p0.x, p1.y - p0.y}, false, ImGuiWindowFlags_NoBackground);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float row_h = c.row_height * z;
    const float width = ImGui::GetContentRegionAvail().x;
    const float fs = c.font_size * z, fs2 = c.font_size * 0.86f * z;
    ImFont* f1 = fontFor(fs);
    ImFont* f2 = fontFor(fs2);
    const std::string playing = player_.getFilePath();
    const bool is_playing = player_.getState() == PlayerState::Playing;

    ImGuiListClipper clip;
    clip.Begin((int)rows.size(), row_h);
    int play_row = -1, remove_index = -1;
    PlaylistEntry fav_toggle;                 // applied after drawing (lists must not change mid-loop)
    bool do_fav = false, do_add = false;
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
            const Row& row = rows[k];
            // What to show
            std::string title, artist, path, art;
            double dur = 0.0;
            if (row.e) {
                title = row.e->title; artist = row.e->artist; path = row.e->path; dur = row.e->duration; art = row.e->art;
                if (!isUrl(path)) {                    // local file: tags + cover from the cache
                    if (const MediaCache::Info* in = media_.get(path); in && in->loaded) {
                        if (!in->title.empty()) title = in->title;
                        if (!in->artist.empty()) artist = in->artist;
                        if (dur <= 0) dur = in->duration;
                    }
                } else if (art.empty()) art = youtubeThumbnail(path);
            } else {
                title = row.r->title; artist = row.r->uploader; path = row.r->url; dur = row.r->duration; art = row.r->thumbnail;
            }
            Fonts::noteText(title);
            Fonts::noteText(artist);
            const bool current = source == "playlist" ? (row.index == playlist_.getIndex() && !playing.empty())
                                                      : (!playing.empty() && path == playing);

            ImGui::PushID(k);
            ImVec2 r0 = ImGui::GetCursorScreenPos(), r1 = {r0.x + width, r0.y + row_h};
            ImGui::InvisibleButton("##row", {width, row_h});
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) play_row = k;

            // Row background
            const ImVec2 q0 = {r0.x + 2.f * z, r0.y + 3.f * z}, q1 = {r1.x - 6.f * z, r1.y - 3.f * z};
            if (current) {
                if (c.glow) glowRect(dl, q0, q1, 8.f * z, c.glow, 6.f * z);
                dl->AddRectFilled(q0, q1, c.bg_on ? c.bg_on : withAlpha(c.fill, 0.18f), 8.f * z);
                if (c.border_on) dl->AddRect(q0, q1, c.border_on, 8.f * z, 0, 1.5f * z);
            } else if (hovered) {
                dl->AddRectFilled(q0, q1, IM_COL32(255, 255, 255, 14), 8.f * z);
            }

            // Cover art
            const float a = row_h - 14.f * z;
            const ImVec2 a0 = {r0.x + 8.f * z, r0.y + 7.f * z}, a1 = {a0.x + a, a0.y + a};
            const MediaCache::Info* in = !art.empty() ? media_.get(art) : !isUrl(path) ? media_.get(path) : nullptr;
            unsigned t = in && in->tex ? in->tex : media_.placeholder();
            ImVec2 uv0, uv1;
            coverUV(in && in->tex ? in->w : 1, in && in->tex ? in->h : 1, a, a, uv0, uv1);
            dl->AddImageRounded(tex(t), a0, a1, uv0, uv1, IM_COL32_WHITE, 5.f * z);

            // Title / artist / duration
            const float tx = a1.x + 14.f * z;
            std::string d = dur > 0 ? formatTime(dur) : "";
            const float dw = d.empty() ? 0.f : f2->CalcTextSizeA(fs2, FLT_MAX, 0.f, d.c_str()).x;
            const float tmax = r1.x - 18.f * z - dw - 14.f * z;
            const float ty = r0.y + row_h * 0.5f - fs * 0.95f;
            dl->PushClipRect({tx, r0.y}, {tmax, r1.y}, true);
            dl->AddText(f1, fs, {tx, ty}, current ? c.color_on ? c.color_on : c.fill : c.color, title.c_str());
            dl->AddText(f2, fs2, {tx, ty + fs * 1.2f}, c.color2, artist.c_str());
            dl->PopClipRect();
            if (current && is_playing) {
                float tw = std::min(f1->CalcTextSizeA(fs, FLT_MAX, 0.f, title.c_str()).x, tmax - tx - 20.f * z);
                icon(dl, "eqbars", {tx + tw + 14.f * z, ty + fs * 0.5f}, fs * 0.45f, c.fill);
            }
            if (!d.empty())
                dl->AddText(f2, fs2, {r1.x - 18.f * z - dw, r0.y + row_h * 0.5f - fs2 * 0.5f},
                            current ? c.fill : c.color2, d.c_str());

            // Right-click menu
            if (ImGui::BeginPopupContextItem("##rowmenu")) {
                PlaylistEntry e;
                if (row.e) e = *row.e; else { e.path = path; e.title = title; e.artist = artist; e.duration = dur; e.art = art; }
                if (ImGui::MenuItem("Play")) play_row = k;
                if (source != "playlist" && ImGui::MenuItem("Add to playlist")) { fav_toggle = e; do_add = true; }
                if (ImGui::MenuItem(isFavorite(e.path) ? "Remove from favorites" : "Add to favorites")) { fav_toggle = e; do_fav = true; }
                if (source == "playlist" && ImGui::MenuItem("Remove")) remove_index = row.index;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);

    if (do_add) playlist_.addEntry(fav_toggle);
    if (do_fav) toggleFavorite(fav_toggle);
    if (remove_index >= 0) { playlist_.removeAt(remove_index); return; }

    // Double-click: play (from other sources: add to the playlist first)
    if (play_row >= 0) {
        const Row& row = rows[play_row];
        if (source == "playlist") { playEntry(row.index); return; }
        PlaylistEntry e;
        if (row.e) e = *row.e;
        else { e.path = row.r->url; e.title = row.r->title; e.artist = row.r->uploader; e.duration = row.r->duration; e.art = row.r->thumbnail; }
        const auto& pl = playlist_.entries();
        auto it = std::find_if(pl.begin(), pl.end(), [&](const PlaylistEntry& x) { return x.path == e.path; });
        if (it != pl.end()) playEntry((int)(it - pl.begin()));
        else { playlist_.addEntry(e); playEntry(playlist_.size() - 1); }
    }
}

// ─── 10-band equalizer sliders ────────────────────────────────────────────────
void UIManager::drawSkinEq(const SkinControl& c, int idx, ImVec2 p0, ImVec2 p1) {
    static const char* labels[Equalizer::BANDS] = { "32", "64", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float z = skin_zoom_;
    const float colw = (p1.x - p0.x) / Equalizer::BANDS;
    const float fs = c.font_size * z;
    const float top = p0.y + 8.f * z, bottom = p1.y - fs - 14.f * z;
    const float dim = eq_.enabled() ? 1.f : 0.45f;
    const float r = c.radius * z;

    for (int b = 0; b < Equalizer::BANDS; ++b) {
        const float x = p0.x + colw * (b + 0.5f);
        const ImU32 col = neon(0.08f + 0.84f * b / (Equalizer::BANDS - 1), dim);
        // Track: fades out towards the top
        const float tw = std::max(2.f, 3.f * z);
        dl->AddRectFilledMultiColor({x - tw * 0.5f, top}, {x + tw * 0.5f, bottom},
                                    withAlpha(col, 0.25f), withAlpha(col, 0.25f), col, col);
        // Knob
        const float g = eq_.gain(b);
        const float y = top + (bottom - top) * (0.5f - g / (2.f * Equalizer::MAX_DB));
        glowCircle(dl, {x, y}, r, col, 7.f * z);
        dl->AddCircleFilled({x, y}, r, withAlpha(IM_COL32(245, 245, 255, 255), dim), 24);
        // Label
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, labels[b]);
        dl->AddText(ImGui::GetFont(), fs, {x - ts.x * 0.5f, p1.y - fs - 2.f * z}, c.color2, labels[b]);

        // Drag to set, double-click to reset
        char id[32];
        std::snprintf(id, sizeof(id), "##eq%d_%d", idx, b);
        const float sx = (x - skin_o_.x) / z - colw / z * 0.5f;
        skinHot(id, sx, (top - skin_o_.y) / z - 10.f, colw / z, (bottom - top) / z + 20.f, nullptr);
        if (ImGui::IsItemActive()) {
            float v = (0.5f - (ImGui::GetIO().MousePos.y - top) / (bottom - top)) * 2.f * Equalizer::MAX_DB;
            eq_.setGain(b, std::round(v * 2.f) / 2.f);
            if (!eq_.enabled()) eq_.setEnabled(true);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) eq_.setGain(b, 0.f);
        if (ImGui::IsItemDeactivated()) saveSettings();
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetTooltip("%s Hz: %+.1f dB", labels[b], eq_.gain(b));
    }
}

// ─── Search box ───────────────────────────────────────────────────────────────
void UIManager::drawSkinSearch(const SkinControl& c, int idx, ImVec2 p0, ImVec2 p1) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float z = skin_zoom_;
    const float h = p1.y - p0.y, r = c.radius * z;
    if (c.glow && ImGui::IsWindowFocused()) glowRect(dl, p0, p1, r, c.glow, 6.f * z);
    dl->AddRectFilled(p0, p1, c.bg ? c.bg : IM_COL32(0, 0, 0, 90), r);
    if (c.border) dl->AddRect(p0, p1, c.border, r, 0, c.border_width * z);
    icon(dl, "search", {p0.x + h * 0.55f, p0.y + h * 0.5f}, h * 0.26f, c.color2);
    skin_hot_.push_back({c.x, c.y, c.w, c.h});

    const float fh = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos({p0.x + h * 0.95f, p0.y + (h - fh) * 0.5f - 3.f});
    ImGui::SetNextItemWidth(p1.x - p0.x - h * 1.15f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, c.color);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, c.color2);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2.f, 3.f});
    char id[32];
    std::snprintf(id, sizeof(id), "##search%d", idx);
    Fonts::noteText(skin_search_);
    const bool enter = ImGui::InputTextWithHint(id, c.hint.empty() ? "Search music..." : c.hint.c_str(),
                                                skin_search_, sizeof(skin_search_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    // Enter searches online when an online list is showing; otherwise the lists filter as you type
    if (enter && skin_search_[0] && (skin_list_ == "online" || skin_page_ == "online") && online_.available()) {
        online_.search(OnlineSource::YouTube, skin_search_);
        search_started_ = ImGui::GetTime();
    }
}

// ─── Equalizer preset dropdown ────────────────────────────────────────────────
void UIManager::drawSkinCombo(const SkinControl& c, int idx, ImVec2 p0, ImVec2 p1) {
    const float z = skin_zoom_;
    skin_hot_.push_back({c.x, c.y, c.w, c.h});
    const float fh = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos(p0);
    ImGui::SetNextItemWidth(p1.x - p0.x);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.bg ? c.bg : IM_COL32(0, 0, 0, 90));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, withAlpha(c.fill, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, c.border ? c.border : IM_COL32(255, 255, 255, 40));
    ImGui::PushStyleColor(ImGuiCol_Text, c.color);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(14, 12, 24, 245));
    ImGui::PushStyleColor(ImGuiCol_Header, withAlpha(c.fill, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, withAlpha(c.fill, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, c.border_width * z);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, c.radius * z);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.f * z, std::max(2.f, (p1.y - p0.y - fh) * 0.5f)});
    char id[32];
    std::snprintf(id, sizeof(id), "##combo%d", idx);
    if (ImGui::BeginCombo(id, Equalizer::presetName(eq_.preset()))) {
        for (int i = 0; i < Equalizer::presetCount(); ++i)
            if (ImGui::Selectable(Equalizer::presetName(i), i == eq_.preset())) {
                eq_.setPreset(i);
                eq_.setEnabled(true);
                saveSettings();
            }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(9);
}

// ─── Equalizer in the settings tab (normal window) ───────────────────────────
void UIManager::drawEqualizerSettings() {
    static const char* labels[Equalizer::BANDS] = { "32", "64", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    ImGui::SeparatorText("Equalizer");
    bool on = eq_.enabled();
    if (ImGui::Checkbox("Enabled##eq", &on)) { eq_.setEnabled(on); saveSettings(); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##eqpreset", Equalizer::presetName(eq_.preset()))) {
        for (int i = 0; i < Equalizer::presetCount(); ++i)
            if (ImGui::Selectable(Equalizer::presetName(i), i == eq_.preset())) { eq_.setPreset(i); saveSettings(); }
        ImGui::EndCombo();
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    const float w = (ImGui::GetContentRegionAvail().x - spacing * (Equalizer::BANDS - 1)) / Equalizer::BANDS;
    ImGui::BeginDisabled(!eq_.enabled());
    for (int b = 0; b < Equalizer::BANDS; ++b) {
        if (b) ImGui::SameLine(0, spacing);
        ImGui::PushID(b);
        float g = eq_.gain(b);
        if (ImGui::VSliderFloat("##g", {w, S(110.f)}, &g, -Equalizer::MAX_DB, Equalizer::MAX_DB, ""))
            eq_.setGain(b, g);
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s Hz: %+.1f dB", labels[b], g);
        ImGui::PopID();
    }
    for (int b = 0; b < Equalizer::BANDS; ++b) {
        if (b) ImGui::SameLine(0, spacing);
        ImVec2 ts = ImGui::CalcTextSize(labels[b]);
        float x = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(x + std::max(0.f, (w - ts.x) * 0.5f));
        ImGui::TextDisabled("%s", labels[b]);
        if (b + 1 < Equalizer::BANDS) { ImGui::SameLine(0, 0); ImGui::SetCursorPosX(x + w); }
    }
    ImGui::EndDisabled();
}
