#pragma once
// ─── theme_manager.h ──────────────────────────────────────────────────────────
// Colour themes for the main window. A theme is a small palette; every ImGui
// colour is derived from it, so themes can be edited live and saved.
// Built-in presets + user themes stored in <appdata>/themes.ini.
// ─────────────────────────────────────────────────────────────────────────────
#include <imgui.h>
#include <string>
#include <vector>

struct ThemeDef {
    std::string name;
    ImU32 bg       = IM_COL32( 13, 13, 13,255); // window background
    ImU32 panel    = IM_COL32( 18, 18, 18,255); // child panels / lists
    ImU32 frame    = IM_COL32( 26, 26, 26,255); // inputs, sliders, buttons base
    ImU32 text     = IM_COL32(230,230,230,255);
    ImU32 text_dim = IM_COL32(120,120,120,255);
    ImU32 accent   = IM_COL32(  0,255,100,255); // primary accent (also visualizer)
    ImU32 accent2  = IM_COL32(  0,180, 60,255); // secondary accent
    float rounding = 3.f;                       // corner radius (px at 1x scale)
    bool  borders  = false;                     // draw frame borders
    bool  builtin  = false;                     // presets can't be deleted/overwritten
};

class ThemeManager {
public:
    ThemeManager();

    // Apply by index into themes()
    void applyTheme(int idx);
    // Apply an edited copy without saving it (live preview in the editor)
    void previewTheme(const ThemeDef& t);
    int  currentIndex() const { return current_; }
    const ThemeDef& current() const { return active_; }
    const std::vector<ThemeDef>& themes() const { return themes_; }

    // UI scale: re-applies the theme with every size multiplied by s
    void  setScale(float s);
    float scale() const { return scale_; }

    ImU32 accentColor()  const { return active_.accent;  }
    ImU32 accent2Color() const { return active_.accent2; }

    // User themes (persisted). Returns the index of the saved theme.
    int  saveCustom(const ThemeDef& t);
    bool deleteCustom(int idx);

private:
    void addPresets();
    void apply(const ThemeDef& t);
    void load();
    void save() const;

    std::vector<ThemeDef> themes_;
    ThemeDef              active_;
    int                   current_ = 0;
    float                 scale_   = 1.f;
};
