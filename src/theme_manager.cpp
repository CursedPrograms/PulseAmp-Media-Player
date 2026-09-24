// ─── theme_manager.cpp ────────────────────────────────────────────────────────
#include "theme_manager.h"

ThemeManager::ThemeManager() {
    buildNeonAmp();
    buildChromePlayer();
    buildMidnightFusion();
    applyTheme(Theme::NeonAmp);
}

void ThemeManager::applyTheme(Theme t) {
    current_ = t;
    themes_[(int)t].apply();
}

ImU32 ThemeManager::accentColor()  const { return themes_[(int)current_].accent;  }
ImU32 ThemeManager::accent2Color() const { return themes_[(int)current_].accent2; }

// ─────────────────────────────────────────────────────────────────────────────
// Theme 0 – NeonAmp  (pitch-black + electric green, Winamp 2.x spirit)
// ─────────────────────────────────────────────────────────────────────────────
void ThemeManager::buildNeonAmp() {
    ThemeDef t;
    t.name    = "NeonAmp";
    t.accent  = IM_COL32(0, 255, 100, 255);
    t.accent2 = IM_COL32(0, 180, 60,  255);
    t.apply = []() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 4.f;
        s.FrameRounding     = 3.f;
        s.ScrollbarRounding = 3.f;
        s.GrabRounding      = 3.f;
        s.WindowBorderSize  = 1.f;
        s.FrameBorderSize   = 0.f;
        s.PopupBorderSize   = 1.f;
        s.ItemSpacing       = {6, 5};
        s.FramePadding      = {6, 4};
        s.WindowPadding     = {8, 8};

        ImVec4* c = s.Colors;
        // Base blacks / dark greens
        c[ImGuiCol_WindowBg]          = {0.05f,0.05f,0.05f,1.00f};
        c[ImGuiCol_ChildBg]           = {0.07f,0.07f,0.07f,1.00f};
        c[ImGuiCol_PopupBg]           = {0.06f,0.06f,0.06f,0.98f};
        c[ImGuiCol_Border]            = {0.00f,0.60f,0.30f,0.50f};
        c[ImGuiCol_BorderShadow]      = {0.00f,0.00f,0.00f,0.00f};
        c[ImGuiCol_FrameBg]           = {0.10f,0.10f,0.10f,1.00f};
        c[ImGuiCol_FrameBgHovered]    = {0.00f,0.40f,0.20f,0.60f};
        c[ImGuiCol_FrameBgActive]     = {0.00f,0.60f,0.30f,0.80f};
        c[ImGuiCol_TitleBg]           = {0.02f,0.12f,0.06f,1.00f};
        c[ImGuiCol_TitleBgActive]     = {0.00f,0.25f,0.12f,1.00f};
        c[ImGuiCol_TitleBgCollapsed]  = {0.02f,0.08f,0.04f,0.80f};
        c[ImGuiCol_MenuBarBg]         = {0.04f,0.08f,0.05f,1.00f};
        c[ImGuiCol_ScrollbarBg]       = {0.05f,0.05f,0.05f,0.90f};
        c[ImGuiCol_ScrollbarGrab]     = {0.00f,0.50f,0.25f,1.00f};
        c[ImGuiCol_ScrollbarGrabHovered]={0.00f,0.70f,0.35f,1.00f};
        c[ImGuiCol_ScrollbarGrabActive]= {0.00f,1.00f,0.50f,1.00f};
        c[ImGuiCol_CheckMark]         = {0.00f,1.00f,0.50f,1.00f};
        c[ImGuiCol_SliderGrab]        = {0.00f,0.90f,0.45f,1.00f};
        c[ImGuiCol_SliderGrabActive]  = {0.00f,1.00f,0.55f,1.00f};
        c[ImGuiCol_Button]            = {0.00f,0.35f,0.17f,1.00f};
        c[ImGuiCol_ButtonHovered]     = {0.00f,0.55f,0.28f,1.00f};
        c[ImGuiCol_ButtonActive]      = {0.00f,0.80f,0.40f,1.00f};
        c[ImGuiCol_Header]            = {0.00f,0.40f,0.20f,0.60f};
        c[ImGuiCol_HeaderHovered]     = {0.00f,0.55f,0.28f,0.80f};
        c[ImGuiCol_HeaderActive]      = {0.00f,0.70f,0.35f,1.00f};
        c[ImGuiCol_Tab]               = {0.05f,0.15f,0.08f,1.00f};
        c[ImGuiCol_TabHovered]        = {0.00f,0.50f,0.25f,1.00f};
        c[ImGuiCol_TabActive]         = {0.00f,0.70f,0.35f,1.00f};
        c[ImGuiCol_TabUnfocused]      = {0.03f,0.10f,0.05f,0.90f};
        c[ImGuiCol_TabUnfocusedActive]= {0.00f,0.40f,0.20f,0.90f};
        c[ImGuiCol_Text]              = {0.00f,1.00f,0.55f,1.00f};
        c[ImGuiCol_TextDisabled]      = {0.30f,0.50f,0.35f,1.00f};
        c[ImGuiCol_Separator]         = {0.00f,0.40f,0.20f,0.60f};
        c[ImGuiCol_ResizeGrip]        = {0.00f,0.70f,0.35f,0.40f};
        c[ImGuiCol_ResizeGripHovered] = {0.00f,0.90f,0.45f,0.70f};
        c[ImGuiCol_ResizeGripActive]  = {0.00f,1.00f,0.55f,1.00f};
        c[ImGuiCol_PlotLines]         = {0.00f,1.00f,0.50f,1.00f};
        c[ImGuiCol_PlotHistogram]     = {0.00f,0.80f,0.40f,1.00f};
        c[ImGuiCol_TableBorderLight]  = {0.00f,0.30f,0.15f,1.00f};
        c[ImGuiCol_TableRowBg]        = {0.05f,0.05f,0.05f,1.00f};
        c[ImGuiCol_TableRowBgAlt]     = {0.08f,0.08f,0.08f,1.00f};
    };
    themes_.push_back(std::move(t));
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme 1 – ChromePlayer  (silver/steel blue, Windows Media Player 9 spirit)
// ─────────────────────────────────────────────────────────────────────────────
void ThemeManager::buildChromePlayer() {
    ThemeDef t;
    t.name    = "ChromePlayer";
    t.accent  = IM_COL32(80, 160, 255, 255);
    t.accent2 = IM_COL32(40, 100, 200, 255);
    t.apply = []() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 6.f;
        s.FrameRounding     = 4.f;
        s.ScrollbarRounding = 6.f;
        s.GrabRounding      = 4.f;
        s.WindowBorderSize  = 1.f;
        s.FrameBorderSize   = 1.f;
        s.PopupBorderSize   = 1.f;
        s.ItemSpacing       = {6, 5};

        ImVec4* c = s.Colors;
        // Silver-grey base, blue accents
        c[ImGuiCol_WindowBg]          = {0.15f,0.16f,0.18f,1.00f};
        c[ImGuiCol_ChildBg]           = {0.13f,0.14f,0.16f,1.00f};
        c[ImGuiCol_PopupBg]           = {0.16f,0.17f,0.20f,0.98f};
        c[ImGuiCol_Border]            = {0.40f,0.50f,0.65f,0.60f};
        c[ImGuiCol_BorderShadow]      = {0.00f,0.00f,0.00f,0.20f};
        c[ImGuiCol_FrameBg]           = {0.20f,0.22f,0.26f,1.00f};
        c[ImGuiCol_FrameBgHovered]    = {0.25f,0.35f,0.55f,0.80f};
        c[ImGuiCol_FrameBgActive]     = {0.25f,0.40f,0.70f,1.00f};
        c[ImGuiCol_TitleBg]           = {0.10f,0.12f,0.18f,1.00f};
        c[ImGuiCol_TitleBgActive]     = {0.15f,0.22f,0.40f,1.00f};
        c[ImGuiCol_TitleBgCollapsed]  = {0.08f,0.10f,0.15f,0.80f};
        c[ImGuiCol_MenuBarBg]         = {0.18f,0.20f,0.25f,1.00f};
        c[ImGuiCol_ScrollbarBg]       = {0.12f,0.13f,0.16f,0.90f};
        c[ImGuiCol_ScrollbarGrab]     = {0.30f,0.45f,0.75f,1.00f};
        c[ImGuiCol_ScrollbarGrabHovered]={0.40f,0.55f,0.90f,1.00f};
        c[ImGuiCol_ScrollbarGrabActive]= {0.50f,0.65f,1.00f,1.00f};
        c[ImGuiCol_CheckMark]         = {0.40f,0.70f,1.00f,1.00f};
        c[ImGuiCol_SliderGrab]        = {0.35f,0.60f,1.00f,1.00f};
        c[ImGuiCol_SliderGrabActive]  = {0.50f,0.75f,1.00f,1.00f};
        c[ImGuiCol_Button]            = {0.22f,0.35f,0.60f,1.00f};
        c[ImGuiCol_ButtonHovered]     = {0.30f,0.48f,0.80f,1.00f};
        c[ImGuiCol_ButtonActive]      = {0.40f,0.60f,1.00f,1.00f};
        c[ImGuiCol_Header]            = {0.25f,0.40f,0.65f,0.70f};
        c[ImGuiCol_HeaderHovered]     = {0.30f,0.50f,0.80f,0.80f};
        c[ImGuiCol_HeaderActive]      = {0.40f,0.60f,1.00f,1.00f};
        c[ImGuiCol_Tab]               = {0.18f,0.25f,0.40f,1.00f};
        c[ImGuiCol_TabHovered]        = {0.28f,0.45f,0.75f,1.00f};
        c[ImGuiCol_TabActive]         = {0.30f,0.50f,0.90f,1.00f};
        c[ImGuiCol_TabUnfocused]      = {0.12f,0.18f,0.30f,0.90f};
        c[ImGuiCol_TabUnfocusedActive]= {0.20f,0.32f,0.55f,0.90f};
        c[ImGuiCol_Text]              = {0.88f,0.92f,1.00f,1.00f};
        c[ImGuiCol_TextDisabled]      = {0.45f,0.50f,0.60f,1.00f};
        c[ImGuiCol_Separator]         = {0.30f,0.40f,0.60f,0.60f};
        c[ImGuiCol_ResizeGrip]        = {0.35f,0.55f,0.90f,0.40f};
        c[ImGuiCol_ResizeGripHovered] = {0.45f,0.65f,1.00f,0.70f};
        c[ImGuiCol_ResizeGripActive]  = {0.55f,0.75f,1.00f,1.00f};
        c[ImGuiCol_PlotLines]         = {0.40f,0.70f,1.00f,1.00f};
        c[ImGuiCol_PlotHistogram]     = {0.30f,0.60f,1.00f,1.00f};
        c[ImGuiCol_TableBorderLight]  = {0.25f,0.35f,0.55f,1.00f};
        c[ImGuiCol_TableRowBg]        = {0.15f,0.16f,0.18f,1.00f};
        c[ImGuiCol_TableRowBgAlt]     = {0.18f,0.19f,0.22f,1.00f};
    };
    themes_.push_back(std::move(t));
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme 2 – MidnightFusion  (deep purple + molten gold — NovPlayer original)
// ─────────────────────────────────────────────────────────────────────────────
void ThemeManager::buildMidnightFusion() {
    ThemeDef t;
    t.name    = "MidnightFusion";
    t.accent  = IM_COL32(255, 180, 0, 255);
    t.accent2 = IM_COL32(200, 120, 0, 255);
    t.apply = []() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 8.f;
        s.FrameRounding     = 5.f;
        s.ScrollbarRounding = 8.f;
        s.GrabRounding      = 5.f;
        s.WindowBorderSize  = 1.f;
        s.FrameBorderSize   = 0.f;
        s.PopupBorderSize   = 1.f;
        s.ItemSpacing       = {6, 5};

        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]          = {0.08f,0.06f,0.12f,1.00f};
        c[ImGuiCol_ChildBg]           = {0.10f,0.08f,0.14f,1.00f};
        c[ImGuiCol_PopupBg]           = {0.09f,0.07f,0.13f,0.98f};
        c[ImGuiCol_Border]            = {0.55f,0.35f,0.00f,0.60f};
        c[ImGuiCol_BorderShadow]      = {0.00f,0.00f,0.00f,0.30f};
        c[ImGuiCol_FrameBg]           = {0.14f,0.10f,0.20f,1.00f};
        c[ImGuiCol_FrameBgHovered]    = {0.40f,0.25f,0.00f,0.70f};
        c[ImGuiCol_FrameBgActive]     = {0.60f,0.40f,0.00f,0.90f};
        c[ImGuiCol_TitleBg]           = {0.05f,0.03f,0.10f,1.00f};
        c[ImGuiCol_TitleBgActive]     = {0.25f,0.15f,0.00f,1.00f};
        c[ImGuiCol_TitleBgCollapsed]  = {0.04f,0.02f,0.08f,0.80f};
        c[ImGuiCol_MenuBarBg]         = {0.10f,0.07f,0.16f,1.00f};
        c[ImGuiCol_ScrollbarBg]       = {0.06f,0.04f,0.10f,0.90f};
        c[ImGuiCol_ScrollbarGrab]     = {0.55f,0.35f,0.00f,1.00f};
        c[ImGuiCol_ScrollbarGrabHovered]={0.75f,0.50f,0.00f,1.00f};
        c[ImGuiCol_ScrollbarGrabActive]= {1.00f,0.70f,0.00f,1.00f};
        c[ImGuiCol_CheckMark]         = {1.00f,0.75f,0.00f,1.00f};
        c[ImGuiCol_SliderGrab]        = {0.90f,0.60f,0.00f,1.00f};
        c[ImGuiCol_SliderGrabActive]  = {1.00f,0.75f,0.00f,1.00f};
        c[ImGuiCol_Button]            = {0.45f,0.25f,0.00f,1.00f};
        c[ImGuiCol_ButtonHovered]     = {0.70f,0.42f,0.00f,1.00f};
        c[ImGuiCol_ButtonActive]      = {1.00f,0.65f,0.00f,1.00f};
        c[ImGuiCol_Header]            = {0.50f,0.30f,0.00f,0.70f};
        c[ImGuiCol_HeaderHovered]     = {0.70f,0.44f,0.00f,0.80f};
        c[ImGuiCol_HeaderActive]      = {1.00f,0.65f,0.00f,1.00f};
        c[ImGuiCol_Tab]               = {0.18f,0.10f,0.28f,1.00f};
        c[ImGuiCol_TabHovered]        = {0.55f,0.35f,0.00f,1.00f};
        c[ImGuiCol_TabActive]         = {0.75f,0.50f,0.00f,1.00f};
        c[ImGuiCol_TabUnfocused]      = {0.12f,0.07f,0.18f,0.90f};
        c[ImGuiCol_TabUnfocusedActive]= {0.40f,0.25f,0.00f,0.90f};
        c[ImGuiCol_Text]              = {1.00f,0.90f,0.60f,1.00f};
        c[ImGuiCol_TextDisabled]      = {0.55f,0.45f,0.20f,1.00f};
        c[ImGuiCol_Separator]         = {0.55f,0.35f,0.00f,0.60f};
        c[ImGuiCol_ResizeGrip]        = {0.80f,0.55f,0.00f,0.40f};
        c[ImGuiCol_ResizeGripHovered] = {1.00f,0.70f,0.00f,0.70f};
        c[ImGuiCol_ResizeGripActive]  = {1.00f,0.80f,0.00f,1.00f};
        c[ImGuiCol_PlotLines]         = {1.00f,0.75f,0.00f,1.00f};
        c[ImGuiCol_PlotHistogram]     = {0.90f,0.60f,0.00f,1.00f};
        c[ImGuiCol_TableBorderLight]  = {0.45f,0.28f,0.00f,1.00f};
        c[ImGuiCol_TableRowBg]        = {0.08f,0.06f,0.12f,1.00f};
        c[ImGuiCol_TableRowBgAlt]     = {0.11f,0.08f,0.16f,1.00f};
    };
    themes_.push_back(std::move(t));
}
