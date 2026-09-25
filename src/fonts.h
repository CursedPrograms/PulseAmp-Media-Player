#pragma once
// ─── fonts.h ──────────────────────────────────────────────────────────────────
// UI font atlas: a main system font plus merged fallbacks for symbols and
// CJK text. CJK fonts have tens of thousands of glyphs, so only characters
// that actually appear (titles, search results) are baked in: the UI reports
// text with noteText() and the atlas is rebuilt when new characters show up.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

namespace Fonts {
    // (Re)build the atlas for a UI scale. Call outside ImGui::NewFrame/Render.
    void build(float scale);

    // Report text that will be displayed; cheap for plain ASCII.
    void noteText(const std::string& utf8);

    // True (once) if noteText() saw characters the atlas doesn't have yet
    bool consumeDirty();
}
