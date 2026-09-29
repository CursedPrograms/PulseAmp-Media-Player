// ─── skin_draw.cpp ────────────────────────────────────────────────────────────
#include "skin_draw.h"
#include "fonts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace SkinDraw {

ImU32 withAlpha(ImU32 col, float m) {
    int a = (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(m, 0.f, 1.f));
    return (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

void icon(ImDrawList* dl, const std::string& icon, ImVec2 c, float s, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    const float t = std::max(1.f, s * 0.16f);
    if (icon == "play") {
        dl->AddTriangleFilled(P(-0.55f, -0.7f), P(-0.55f, 0.7f), P(0.75f, 0.f), col);
    } else if (icon == "pause") {
        dl->AddRectFilled(P(-0.55f, -0.7f), P(-0.18f, 0.7f), col, s * 0.12f);
        dl->AddRectFilled(P(0.18f, -0.7f), P(0.55f, 0.7f), col, s * 0.12f);
    } else if (icon == "stop") {
        dl->AddRectFilled(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, s * 0.1f);
    } else if (icon == "next") {
        dl->AddTriangleFilled(P(-0.7f, -0.65f), P(-0.7f, 0.65f), P(0.35f, 0.f), col);
        dl->AddRectFilled(P(0.4f, -0.65f), P(0.65f, 0.65f), col);
    } else if (icon == "prev") {
        dl->AddTriangleFilled(P(0.7f, -0.65f), P(0.7f, 0.65f), P(-0.35f, 0.f), col);
        dl->AddRectFilled(P(-0.65f, -0.65f), P(-0.4f, 0.65f), col);
    } else if (icon == "open" || icon == "eject") {
        dl->AddTriangleFilled(P(-0.7f, 0.15f), P(0.7f, 0.15f), P(0.f, -0.7f), col);
        dl->AddRectFilled(P(-0.7f, 0.35f), P(0.7f, 0.6f), col);
    } else if (icon == "close") {
        dl->AddLine(P(-0.55f, -0.55f), P(0.55f, 0.55f), col, t);
        dl->AddLine(P(0.55f, -0.55f), P(-0.55f, 0.55f), col, t);
    } else if (icon == "minimize") {
        dl->AddLine(P(-0.6f, 0.f), P(0.6f, 0.f), col, t);
    } else if (icon == "maximize") {
        dl->AddRect(P(-0.55f, -0.55f), P(0.55f, 0.55f), col, 0.f, 0, t);
    } else if (icon == "menu") {
        for (float y : { -0.45f, 0.f, 0.45f }) dl->AddLine(P(-0.6f, y), P(0.6f, y), col, t);
    } else if (icon == "more") {
        for (float x : { -0.6f, 0.f, 0.6f }) dl->AddCircleFilled(P(x, 0.f), s * 0.13f, col);
    } else if (icon == "plus") {
        dl->AddLine(P(-0.65f, 0.f), P(0.65f, 0.f), col, t);
        dl->AddLine(P(0.f, -0.65f), P(0.f, 0.65f), col, t);
    } else if (icon == "fullwindow") {
        dl->AddRect(P(-0.6f, -0.5f), P(0.6f, 0.5f), col, 0.f, 0, t);
        dl->AddLine(P(-0.6f, -0.25f), P(0.6f, -0.25f), col, t);
    } else if (icon == "fullscreen") {
        for (int sx : { -1, 1 }) for (int sy : { -1, 1 }) {
            dl->AddLine(P(sx * 0.65f, sy * 0.65f), P(sx * 0.25f, sy * 0.65f), col, t);
            dl->AddLine(P(sx * 0.65f, sy * 0.65f), P(sx * 0.65f, sy * 0.25f), col, t);
        }
    } else if (icon == "mute" || icon == "volume") {
        dl->PathLineTo(P(-0.75f, -0.25f)); dl->PathLineTo(P(-0.4f, -0.25f)); dl->PathLineTo(P(0.f, -0.65f));
        dl->PathLineTo(P(0.f, 0.65f)); dl->PathLineTo(P(-0.4f, 0.25f)); dl->PathLineTo(P(-0.75f, 0.25f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        if (icon == "mute") {
            dl->AddLine(P(0.3f, -0.3f), P(0.8f, 0.3f), col, t);
            dl->AddLine(P(0.8f, -0.3f), P(0.3f, 0.3f), col, t);
        } else {
            dl->PathArcTo(P(0.05f, 0.f), s * 0.4f, -0.8f, 0.8f); dl->PathStroke(col, 0, t);
            dl->PathArcTo(P(0.05f, 0.f), s * 0.72f, -0.8f, 0.8f); dl->PathStroke(col, 0, t);
        }
    } else if (icon == "shuffle") {
        dl->AddBezierCubic(P(-0.8f, -0.45f), P(-0.2f, -0.45f), P(0.1f, 0.45f), P(0.65f, 0.45f), col, t);
        dl->AddBezierCubic(P(-0.8f, 0.45f), P(-0.2f, 0.45f), P(0.1f, -0.45f), P(0.65f, -0.45f), col, t);
        dl->AddTriangleFilled(P(0.85f, 0.45f), P(0.5f, 0.2f), P(0.5f, 0.7f), col);
        dl->AddTriangleFilled(P(0.85f, -0.45f), P(0.5f, -0.2f), P(0.5f, -0.7f), col);
    } else if (icon == "repeat") {
        dl->PathArcTo(c, s * 0.62f, -2.6f, 1.9f); dl->PathStroke(col, 0, t);
        ImVec2 tip = P(std::cos(1.9f) * 0.62f, std::sin(1.9f) * 0.62f);
        dl->AddTriangleFilled({tip.x - s * 0.3f, tip.y - s * 0.02f}, {tip.x + s * 0.12f, tip.y - s * 0.3f},
                              {tip.x + s * 0.12f, tip.y + s * 0.28f}, col);
    } else if (icon == "preset_next" || icon == "viz") {
        dl->AddCircle(c, s * 0.6f, col, 0, t);
        dl->AddCircleFilled(c, s * 0.2f, col);
    } else if (icon == "heart" || icon == "heart_filled") {
        if (icon == "heart_filled") {
            // Two lobes + a point (the notch makes the outline concave)
            dl->AddCircleFilled(P(-0.35f, -0.28f), s * 0.37f, col);
            dl->AddCircleFilled(P(0.35f, -0.28f), s * 0.37f, col);
            dl->AddTriangleFilled(P(-0.71f, -0.2f), P(0.71f, -0.2f), P(0.f, 0.75f), col);
        } else {
            dl->PathArcTo(P(-0.35f, -0.28f), s * 0.37f, 3.14159f * 0.75f, 3.14159f * 2.f);
            dl->PathArcTo(P(0.35f, -0.28f), s * 0.37f, 3.14159f, 3.14159f * 2.25f);
            dl->PathLineTo(P(0.f, 0.75f));
            dl->PathStroke(col, ImDrawFlags_Closed, t);
        }
    } else if (icon == "gear") {
        for (int i = 0; i < 8; ++i) {
            float a = i * 3.14159f / 4.f;
            dl->AddLine(P(std::cos(a) * 0.5f, std::sin(a) * 0.5f), P(std::cos(a) * 0.8f, std::sin(a) * 0.8f), col, t * 1.8f);
        }
        dl->AddCircle(c, s * 0.52f, col, 0, t);
        dl->AddCircle(c, s * 0.2f, col, 0, t);
    } else if (icon == "search") {
        dl->AddCircle(P(-0.15f, -0.15f), s * 0.48f, col, 0, t);
        dl->AddLine(P(0.2f, 0.2f), P(0.7f, 0.7f), col, t * 1.2f);
    } else if (icon == "pulse") {
        const ImVec2 pts[] = { P(-1.f, 0.05f), P(-0.55f, 0.05f), P(-0.35f, -0.5f), P(-0.05f, 0.75f),
                               P(0.25f, -0.25f), P(0.45f, 0.05f), P(1.f, 0.05f) };
        dl->AddPolyline(pts, 7, col, 0, t * 1.4f);
    } else if (icon == "eqbars") {
        const float tm = (float)ImGui::GetTime();
        for (int i = 0; i < 3; ++i) {
            float h = 0.35f + 0.55f * (0.5f + 0.5f * std::sin(tm * (5.f + i * 1.7f) + i * 2.1f));
            float x = -0.55f + i * 0.55f;
            dl->AddRectFilled(P(x - 0.16f, 0.7f - h * 1.4f), P(x + 0.16f, 0.7f), col, s * 0.05f);
        }
    }
}

void glowRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImU32 col, float size) {
    if (!col || size <= 0.f) return;
    const int layers = 7;
    for (int i = layers; i >= 1; --i) {
        const float e = size * i / layers;
        const float a = 0.22f * (1.f - (float)(i - 1) / layers) * (1.f - (float)(i - 1) / layers);
        dl->AddRect({p0.x - e, p0.y - e}, {p1.x + e, p1.y + e}, withAlpha(col, a), rounding + e, 0,
                    size / layers * 1.8f);
    }
}

void glowCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float size) {
    if (!col || size <= 0.f) return;
    const int layers = 7;
    for (int i = layers; i >= 1; --i) {
        const float e = size * i / layers;
        const float a = 0.25f * (1.f - (float)(i - 1) / layers) * (1.f - (float)(i - 1) / layers);
        dl->AddCircle(c, r + e, withAlpha(col, a), 0, size / layers * 1.8f);
    }
}

void glowLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float size) {
    if (!col) return;
    for (int i = 3; i >= 1; --i)
        dl->AddLine(a, b, withAlpha(col, 0.12f * (4 - i) / 3.f), thickness + size * i / 3.f);
}

void glowIcon(ImDrawList* dl, const std::string& name, ImVec2 c, float s, ImU32 col) {
    icon(dl, name, c, s * 1.18f, withAlpha(col, 0.18f));
    icon(dl, name, c, s * 1.08f, withAlpha(col, 0.28f));
}

ImFont* fontFor(float size) {
    ImFont* big = Fonts::large();
    return (big && size > ImGui::GetFont()->FontSize * 1.35f) ? big : ImGui::GetFont();
}

// Split "{#rrggbb}" markup into (colour, text) runs
template <class F>
static void forRuns(const std::string& s, ImU32 base, F&& fn) {
    ImU32 col = base;
    size_t i = 0, start = 0;
    while (i < s.size()) {
        if (s[i] == '{' && i + 8 < s.size() && s[i + 1] == '#' && s[i + 8] == '}') {
            if (i > start) fn(col, s.substr(start, i - start));
            unsigned r, g, b;
            if (std::sscanf(s.c_str() + i + 2, "%2x%2x%2x", &r, &g, &b) == 3) col = IM_COL32(r, g, b, 255);
            i += 9;
            start = i;
            continue;
        }
        ++i;
    }
    if (start < s.size()) fn(col, s.substr(start));
}

ImVec2 textSize(ImFont* font, float size, const std::string& t) {
    ImVec2 total{0, size};
    forRuns(t, 0, [&](ImU32, const std::string& run) {
        ImVec2 r = font->CalcTextSizeA(size, FLT_MAX, 0.f, run.c_str());
        total.x += r.x;
        total.y = std::max(total.y, r.y);
    });
    return total;
}

void text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& t, ImU32 glow) {
    auto draw = [&](ImVec2 at, bool halo) {
        float x = at.x;
        forRuns(t, col, [&](ImU32 c, const std::string& run) {
            ImU32 use = halo ? withAlpha(glow ? glow : c, 0.16f) : c;
            dl->AddText(font, size, {x, at.y}, use, run.c_str());
            x += font->CalcTextSizeA(size, FLT_MAX, 0.f, run.c_str()).x;
        });
    };
    if (glow) {
        const float r = std::max(1.f, size * 0.09f);
        for (int k = 0; k < 8; ++k) {
            float a = k * 3.14159f / 4.f;
            draw({pos.x + std::cos(a) * r, pos.y + std::sin(a) * r}, true);
        }
    }
    draw(pos, false);
}

} // namespace SkinDraw
