// ─── theme_manager.cpp ────────────────────────────────────────────────────────
#include "theme_manager.h"
#include "paths.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

// ─── Colour helpers ───────────────────────────────────────────────────────────
static ImVec4 v4(ImU32 c, float a = 1.f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return v;
}
static ImVec4 mix(ImU32 a, ImU32 b, float t, float alpha = 1.f) {
    ImVec4 x = v4(a), y = v4(b);
    return { x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, alpha };
}
static ImU32 hex(const char* s) {
    unsigned r = 0, g = 0, b = 0;
    if (s && *s == '#') ++s;
    if (!s || std::sscanf(s, "%2x%2x%2x", &r, &g, &b) != 3) return IM_COL32(255, 0, 255, 255);
    return IM_COL32(r, g, b, 255);
}
static std::string hexStr(ImU32 c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
        (c >> IM_COL32_R_SHIFT) & 0xFF, (c >> IM_COL32_G_SHIFT) & 0xFF, (c >> IM_COL32_B_SHIFT) & 0xFF);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
ThemeManager::ThemeManager() {
    addPresets();
    load();
    applyTheme(current_);
}

void ThemeManager::addPresets() {
    auto preset = [&](const char* name, const char* bg, const char* panel, const char* frame,
                      const char* text, const char* dim, const char* acc, const char* acc2,
                      float rounding, bool borders) {
        ThemeDef t;
        t.name = name;
        t.bg = hex(bg); t.panel = hex(panel); t.frame = hex(frame);
        t.text = hex(text); t.text_dim = hex(dim);
        t.accent = hex(acc); t.accent2 = hex(acc2);
        t.rounding = rounding; t.borders = borders; t.builtin = true;
        themes_.push_back(t);
    };
    //      name              bg         panel      frame      text       dim        accent     accent2   round border
    preset("NeonAmp",        "#0d0d0d", "#121212", "#1a1a1a", "#00ff8c", "#4d8059", "#00ff64", "#00b43c", 3, false); // Winamp 2.x spirit
    preset("ChromePlayer",   "#26292e", "#212429", "#333842", "#e0ebff", "#737f99", "#50a0ff", "#2864c8", 4, true);  // WMP 9 spirit
    preset("MidnightFusion", "#140f1f", "#1a1424", "#241a33", "#ffe699", "#8c7333", "#ffb400", "#c87800", 5, false);
    preset("Blood Moon",     "#0f0505", "#170808", "#220b0b", "#ffd6d6", "#8a4a4a", "#ff2a2a", "#a00000", 2, false);
    preset("Vaporwave",      "#1a0f2e", "#221338", "#2e1a4a", "#f0e6ff", "#9a7fbf", "#ff71ce", "#01cdfe", 6, false);
    preset("Arctic",         "#0e1a22", "#12222c", "#1a3040", "#e6f7ff", "#6f93a6", "#6fe3ff", "#2aa8d8", 8, false);
    preset("Amber Terminal", "#0a0800", "#100c00", "#1a1400", "#ffb000", "#806020", "#ffb000", "#b07800", 0, true);
    preset("Classic Silver", "#d4d0c8", "#e4e0d8", "#ffffff", "#101010", "#606060", "#0a246a", "#3a6ea5", 0, true);  // light theme
}

void ThemeManager::applyTheme(int idx) {
    if (idx < 0 || idx >= (int)themes_.size()) idx = 0;
    current_ = idx;
    apply(themes_[idx]);
    save(); // remember the choice
}

void ThemeManager::previewTheme(const ThemeDef& t) { apply(t); }

void ThemeManager::setScale(float s) {
    scale_ = s;
    apply(active_);
}

void ThemeManager::apply(const ThemeDef& t) {
    active_ = t;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle(); // reset sizes, then scale from the defaults

    s.WindowRounding    = t.rounding + 1.f;
    s.ChildRounding     = t.rounding;
    s.FrameRounding     = t.rounding;
    s.PopupRounding     = t.rounding;
    s.ScrollbarRounding = t.rounding + 2.f;
    s.GrabRounding      = t.rounding;
    s.TabRounding       = t.rounding;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = t.borders ? 1.f : 0.f;
    s.PopupBorderSize   = 1.f;
    s.ItemSpacing       = {6, 5};
    s.FramePadding      = {6, 4};
    s.WindowPadding     = {8, 8};
    s.ScaleAllSizes(scale_);

    const ImU32 bg = t.bg, fr = t.frame, ac = t.accent;
    ImVec4* c = s.Colors;
    // Accent fills sit behind text: on light backgrounds keep them light
    ImVec4 bgv = v4(bg);
    const bool  light = 0.299f * bgv.x + 0.587f * bgv.y + 0.114f * bgv.z > 0.5f;
    const float k = light ? 0.45f : 1.f;
    auto mix = [&](ImU32 a, ImU32 b, float t_, float alpha = 1.f) {
        return ::mix(a, b, b == ac ? t_ * k : t_, alpha);
    };
    c[ImGuiCol_Text]                 = v4(t.text);
    c[ImGuiCol_TextDisabled]         = v4(t.text_dim);
    c[ImGuiCol_WindowBg]             = v4(bg);
    c[ImGuiCol_ChildBg]              = v4(t.panel);
    c[ImGuiCol_PopupBg]              = mix(bg, t.text, 0.03f, 0.98f);
    c[ImGuiCol_Border]               = mix(bg, ac, 0.5f, 0.5f);
    c[ImGuiCol_BorderShadow]         = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg]              = v4(fr);
    c[ImGuiCol_FrameBgHovered]       = mix(fr, ac, 0.30f);
    c[ImGuiCol_FrameBgActive]        = mix(fr, ac, 0.50f);
    c[ImGuiCol_TitleBg]              = mix(bg, ac, 0.10f);
    c[ImGuiCol_TitleBgActive]        = mix(bg, ac, 0.25f);
    c[ImGuiCol_TitleBgCollapsed]     = mix(bg, ac, 0.05f, 0.8f);
    c[ImGuiCol_MenuBarBg]            = mix(bg, ac, 0.06f);
    c[ImGuiCol_ScrollbarBg]          = v4(bg, 0.9f);
    c[ImGuiCol_ScrollbarGrab]        = mix(fr, ac, 0.50f);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(fr, ac, 0.75f);
    c[ImGuiCol_ScrollbarGrabActive]  = v4(ac);
    c[ImGuiCol_CheckMark]            = v4(ac);
    c[ImGuiCol_SliderGrab]           = mix(fr, ac, 0.85f);
    c[ImGuiCol_SliderGrabActive]     = v4(ac);
    c[ImGuiCol_Button]               = mix(fr, ac, 0.30f);
    c[ImGuiCol_ButtonHovered]        = mix(fr, ac, 0.50f);
    c[ImGuiCol_ButtonActive]         = mix(fr, ac, 0.75f);
    c[ImGuiCol_Header]               = mix(bg, ac, 0.35f, 0.6f);
    c[ImGuiCol_HeaderHovered]        = mix(bg, ac, 0.50f, 0.8f);
    c[ImGuiCol_HeaderActive]         = mix(bg, ac, 0.70f);
    c[ImGuiCol_Separator]            = mix(bg, ac, 0.40f, 0.6f);
    c[ImGuiCol_SeparatorHovered]     = mix(bg, ac, 0.70f);
    c[ImGuiCol_SeparatorActive]      = v4(ac);
    c[ImGuiCol_ResizeGrip]           = v4(ac, 0.4f);
    c[ImGuiCol_ResizeGripHovered]    = v4(ac, 0.7f);
    c[ImGuiCol_ResizeGripActive]     = v4(ac);
    c[ImGuiCol_Tab]                  = mix(bg, ac, 0.15f);
    c[ImGuiCol_TabHovered]           = mix(bg, ac, 0.50f);
    c[ImGuiCol_TabActive]            = mix(bg, ac, 0.65f);
    c[ImGuiCol_TabUnfocused]         = mix(bg, ac, 0.10f, 0.9f);
    c[ImGuiCol_TabUnfocusedActive]   = mix(bg, ac, 0.40f, 0.9f);
    c[ImGuiCol_PlotLines]            = v4(ac);
    c[ImGuiCol_PlotLinesHovered]     = v4(t.accent2);
    c[ImGuiCol_PlotHistogram]        = v4(ac);
    c[ImGuiCol_PlotHistogramHovered] = v4(t.accent2);
    c[ImGuiCol_TableHeaderBg]        = mix(bg, ac, 0.20f);
    c[ImGuiCol_TableBorderStrong]    = mix(bg, ac, 0.35f);
    c[ImGuiCol_TableBorderLight]     = mix(bg, ac, 0.20f);
    c[ImGuiCol_TableRowBg]           = v4(bg);
    c[ImGuiCol_TableRowBgAlt]        = mix(bg, t.text, 0.03f);
    c[ImGuiCol_TextSelectedBg]       = v4(ac, 0.35f);
    c[ImGuiCol_DragDropTarget]       = v4(ac, 0.9f);
    c[ImGuiCol_NavHighlight]         = v4(ac);
    c[ImGuiCol_NavWindowingHighlight]= v4(t.text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg]    = {0.f, 0.f, 0.f, 0.3f};
    c[ImGuiCol_ModalWindowDimBg]     = {0.f, 0.f, 0.f, 0.5f};
}

// ─── User themes: <appdata>/themes.ini ────────────────────────────────────────
//   current=<name>
//   [My Theme]
//   bg=#rrggbb  ...  rounding=4  borders=0
int ThemeManager::saveCustom(const ThemeDef& in) {
    ThemeDef t = in;
    t.builtin = false;
    if (t.name.empty()) t.name = "Custom";
    // Never overwrite a preset: rename instead
    for (auto& e : themes_)
        if (e.name == t.name && e.builtin) { t.name += " (custom)"; break; }

    int idx = -1;
    for (int i = 0; i < (int)themes_.size(); ++i)
        if (themes_[i].name == t.name) { themes_[i] = t; idx = i; }
    if (idx < 0) { themes_.push_back(t); idx = (int)themes_.size() - 1; }
    applyTheme(idx); // also saves
    return idx;
}

bool ThemeManager::deleteCustom(int idx) {
    if (idx < 0 || idx >= (int)themes_.size() || themes_[idx].builtin) return false;
    themes_.erase(themes_.begin() + idx);
    applyTheme(current_ == idx ? 0 : (current_ > idx ? current_ - 1 : current_));
    return true;
}

void ThemeManager::save() const {
    std::ofstream f(appDataPath("themes.ini"));
    if (!f) return;
    f << "current=" << themes_[current_].name << "\n";
    for (auto& t : themes_) {
        if (t.builtin) continue;
        f << "\n[" << t.name << "]\n"
          << "bg="       << hexStr(t.bg)       << "\n"
          << "panel="    << hexStr(t.panel)    << "\n"
          << "frame="    << hexStr(t.frame)    << "\n"
          << "text="     << hexStr(t.text)     << "\n"
          << "text_dim=" << hexStr(t.text_dim) << "\n"
          << "accent="   << hexStr(t.accent)   << "\n"
          << "accent2="  << hexStr(t.accent2)  << "\n"
          << "rounding=" << t.rounding         << "\n"
          << "borders="  << (t.borders ? 1 : 0) << "\n";
    }
}

void ThemeManager::load() {
    std::ifstream f(appDataPath("themes.ini"));
    if (!f) return;
    std::string line, current_name;
    ThemeDef* t = nullptr;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            ThemeDef nt;
            nt.name = line.substr(1, line.size() - 2);
            themes_.push_back(nt);
            t = &themes_.back();
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (!t) { if (k == "current") current_name = v; continue; }
        if      (k == "bg")       t->bg       = hex(v.c_str());
        else if (k == "panel")    t->panel    = hex(v.c_str());
        else if (k == "frame")    t->frame    = hex(v.c_str());
        else if (k == "text")     t->text     = hex(v.c_str());
        else if (k == "text_dim") t->text_dim = hex(v.c_str());
        else if (k == "accent")   t->accent   = hex(v.c_str());
        else if (k == "accent2")  t->accent2  = hex(v.c_str());
        else if (k == "rounding") t->rounding = std::clamp((float)std::atof(v.c_str()), 0.f, 12.f);
        else if (k == "borders")  t->borders  = v == "1";
    }
    for (int i = 0; i < (int)themes_.size(); ++i)
        if (themes_[i].name == current_name) current_ = i;
}
