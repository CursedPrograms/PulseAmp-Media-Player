// ─── fonts.cpp ────────────────────────────────────────────────────────────────
#include "fonts.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <set>

namespace {
std::mutex         mu;
std::set<ImWchar>  cjk_chars;   // CJK / wide characters seen so far
bool               dirty = false;

// Wide scripts that live in the CJK fallback font
bool isCjk(unsigned cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) ||   // radicals, kana, CJK ideographs
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   // Hangul syllables
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // compatibility ideographs
           (cp >= 0xFF00 && cp <= 0xFFEF);     // full-width forms
}

bool exists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}
} // namespace

void Fonts::noteText(const std::string& s) {
    const unsigned char* p = (const unsigned char*)s.c_str();
    std::lock_guard lk(mu);
    while (*p) {
        if (*p < 0x80) { ++p; continue; }                 // ASCII: nothing to do
        // Decode one UTF-8 sequence (2-4 bytes); skip invalid bytes
        int n = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
        unsigned cp = n == 4 ? (*p & 0x07u) : n == 3 ? (*p & 0x0Fu) : (*p & 0x1Fu);
        int i = 1;
        for (; i < n && (p[i] & 0xC0) == 0x80; ++i) cp = (cp << 6) | (p[i] & 0x3Fu);
        if (n == 1 || i < n) { ++p; continue; }
        p += n;
        if (cp <= 0xFFFF && isCjk(cp) && cjk_chars.insert((ImWchar)cp).second) dirty = true;
    }
}

bool Fonts::consumeDirty() {
    std::lock_guard lk(mu);
    bool d = dirty;
    dirty = false;
    return d;
}

void Fonts::build(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Main font: Latin (incl. extended), Greek, Cyrillic, punctuation, arrows
    static const ImWchar main_ranges[] = {
        0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x052F, 0x1E00, 0x1EFF,
        0x2000, 0x206F, 0x20A0, 0x20CF, 0x2100, 0x21FF, 0 };
    // Symbols: shapes, dingbats (★ ✨ ♪ ...) common in online titles
    static const ImWchar symbol_ranges[] = {
        0x2200, 0x22FF, 0x2300, 0x23FF, 0x2460, 0x24FF, 0x2500, 0x27BF, 0x2B00, 0x2BFF, 0 };
    // CJK: only the characters seen so far
    static ImVector<ImWchar> cjk_ranges;
    {
        std::lock_guard lk(mu);
        ImFontGlyphRangesBuilder b;
        for (ImWchar c : cjk_chars) b.AddChar(c);
        cjk_ranges.clear();
        b.BuildRanges(&cjk_ranges);
    }

#ifdef _WIN32
    const char* main_font = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* sym_font  = "C:\\Windows\\Fonts\\seguisym.ttf";
    const char* cjk_fonts[] = { "C:\\Windows\\Fonts\\msyh.ttc",      // Microsoft YaHei
                                "C:\\Windows\\Fonts\\YuGothM.ttc",   // Yu Gothic
                                "C:\\Windows\\Fonts\\malgun.ttf" };  // Malgun Gothic (Korean)
#elif __APPLE__
    const char* main_font = "/System/Library/Fonts/Supplemental/Arial.ttf";
    const char* sym_font  = "/System/Library/Fonts/Apple Symbols.ttf";
    const char* cjk_fonts[] = { "/System/Library/Fonts/Hiragino Sans GB.ttc",
                                "/System/Library/Fonts/AppleSDGothicNeo.ttc" };
#else
    // First font that exists (Debian/Ubuntu, Fedora, Arch locations)
    static const char* main_fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" };
    const char* main_font = main_fonts[0];
    for (const char* f : main_fonts) if (exists(f)) { main_font = f; break; }
    const char* sym_font  = main_font;
    const char* cjk_fonts[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
                                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf" };
#endif

    const float px = std::round(15.f * scale);
    if (!exists(main_font) || !io.Fonts->AddFontFromFileTTF(main_font, px, nullptr, main_ranges)) {
        ImFontConfig cfg;
        cfg.SizePixels = std::round(13.f * scale);
        io.Fonts->AddFontDefault(&cfg);
        return;
    }

    ImFontConfig merge;
    merge.MergeMode = true;
    if (exists(sym_font))
        io.Fonts->AddFontFromFileTTF(sym_font, px, &merge, symbol_ranges);
    if (cjk_ranges.Size > 1)
        for (const char* f : cjk_fonts)
            if (exists(f)) {
                io.Fonts->AddFontFromFileTTF(f, px, &merge, cjk_ranges.Data);
                break;
            }
}
