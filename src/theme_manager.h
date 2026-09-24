#pragma once
// ─── theme_manager.h ──────────────────────────────────────────────────────────
#include <imgui.h>
#include <string>
#include <vector>
#include <functional>

enum class Theme { NeonAmp = 0, ChromePlayer = 1, MidnightFusion = 2 };

struct ThemeDef {
    std::string name;
    ImU32       accent;       // primary accent colour (also used by visualizer)
    ImU32       accent2;      // secondary accent
    std::function<void()> apply; // mutates ImGui style
};

class ThemeManager {
public:
    ThemeManager();

    void applyTheme(Theme t);
    Theme currentTheme() const { return current_; }
    const std::vector<ThemeDef>& themes() const { return themes_; }

    ImU32 accentColor()  const;
    ImU32 accent2Color() const;

private:
    void buildNeonAmp();
    void buildChromePlayer();
    void buildMidnightFusion();

    std::vector<ThemeDef> themes_;
    Theme                 current_ = Theme::NeonAmp;
};
