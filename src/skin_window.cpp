// ─── skin_window.cpp ──────────────────────────────────────────────────────────
#include "skin_window.h"
#include <SDL2/SDL_syswm.h>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void setWindowShape(SDL_Window* win, const std::vector<uint8_t>* mask, int mw, int mh, float zoom) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    HWND hwnd = info.info.win.window;

    if (!mask || mask->size() < (size_t)mw * mh) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }

    // Horizontal runs per row; identical consecutive rows become one rectangle
    auto sx = [&](int x) { return (LONG)std::lround(x * zoom); };
    std::vector<RECT> rects;
    std::vector<std::pair<int,int>> prev_runs, runs;
    int band_start = 0;
    auto flush = [&](int y_end) {
        for (auto [a, b] : prev_runs)
            rects.push_back({ sx(a), sx(band_start), sx(b), sx(y_end) });
    };
    for (int y = 0; y <= mh; ++y) {
        runs.clear();
        if (y < mh) {
            const uint8_t* row = mask->data() + (size_t)y * mw;
            for (int x = 0; x < mw;) {
                while (x < mw && !row[x]) ++x;
                int start = x;
                while (x < mw && row[x]) ++x;
                if (x > start) runs.push_back({ start, x });
            }
        }
        if (y == mh || runs != prev_runs) {
            flush(y);
            prev_runs = runs;
            band_start = y;
        }
    }

    // One region from all rectangles (much faster than combining one by one)
    std::vector<char> buf(sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT));
    auto* rd = reinterpret_cast<RGNDATA*>(buf.data());
    rd->rdh.dwSize = sizeof(RGNDATAHEADER);
    rd->rdh.iType = RDH_RECTANGLES;
    rd->rdh.nCount = (DWORD)rects.size();
    rd->rdh.nRgnSize = (DWORD)(rects.size() * sizeof(RECT));
    rd->rdh.rcBound = { 0, 0, sx(mw), sx(mh) };
    std::copy(rects.begin(), rects.end(), reinterpret_cast<RECT*>(rd->Buffer));
    HRGN rgn = ExtCreateRegion(nullptr, (DWORD)buf.size(), rd);
    if (rgn) SetWindowRgn(hwnd, rgn, TRUE);   // the window owns rgn from now on
}

#else
void setWindowShape(SDL_Window*, const std::vector<uint8_t>*, int, int, float) {}
#endif
