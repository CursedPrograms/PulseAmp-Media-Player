#pragma once
// ─── skin_draw.h ──────────────────────────────────────────────────────────────
// Drawing helpers for PulseAmp skins: vector icons, neon glow and text with
// inline colours ("Pulse{#ff3cc8}Amp").
// ─────────────────────────────────────────────────────────────────────────────
#include <imgui.h>
#include <string>

namespace SkinDraw {

ImU32 withAlpha(ImU32 col, float alpha_mul);

// Vector icon centred at c, s = half size. Icons: play pause stop next prev open
// close minimize maximize menu more fullwindow fullscreen volume mute shuffle
// repeat viz heart heart_filled gear search plus pulse eqbars
void icon(ImDrawList* dl, const std::string& name, ImVec2 c, float s, ImU32 col);

// Soft neon glow around a (rounded) rectangle / circle / line / icon
void glowRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImU32 col, float size);
void glowCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float size);
void glowLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float size);
void glowIcon(ImDrawList* dl, const std::string& name, ImVec2 c, float s, ImU32 col);

// Text with {#rrggbb} colour switches; returns the size (markup excluded).
// glow != 0 draws a soft halo in that colour behind the text.
ImVec2 textSize(ImFont* font, float size, const std::string& text);
void   text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
            const std::string& text, ImU32 glow = 0);

// Font for a pixel size: the large atlas font for big text, else the UI font
ImFont* fontFor(float size);

} // namespace SkinDraw
