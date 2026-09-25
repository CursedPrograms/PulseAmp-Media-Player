// ─── skin_window.cpp ──────────────────────────────────────────────────────────
#include "skin_window.h"
#include <SDL2/SDL_syswm.h>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(PULSEAMP_HAVE_XSHAPE)
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

namespace {

struct Rect { long x0, y0, x1, y1; };

// Horizontal runs of the mask; identical consecutive rows merge into one rectangle
[[maybe_unused]] std::vector<Rect> maskToRects(const std::vector<uint8_t>& mask, int mw, int mh, float zoom) {
    auto sx = [&](int v) { return (long)std::lround(v * zoom); };
    std::vector<Rect> rects;
    std::vector<std::pair<int,int>> prev_runs, runs;
    int band_start = 0;
    for (int y = 0; y <= mh; ++y) {
        runs.clear();
        if (y < mh) {
            const uint8_t* row = mask.data() + (size_t)y * mw;
            for (int x = 0; x < mw;) {
                while (x < mw && !row[x]) ++x;
                int start = x;
                while (x < mw && row[x]) ++x;
                if (x > start) runs.push_back({ start, x });
            }
        }
        if (y == mh || runs != prev_runs) {
            for (auto [a, b] : prev_runs)
                rects.push_back({ sx(a), sx(band_start), sx(b), sx(y) });
            prev_runs = runs;
            band_start = y;
        }
    }
    return rects;
}

} // namespace

#ifdef _WIN32
void setWindowShape(SDL_Window* win, const std::vector<uint8_t>* mask, int mw, int mh, float zoom) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    HWND hwnd = info.info.win.window;

    if (!mask || mask->size() < (size_t)mw * mh) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    std::vector<Rect> rects = maskToRects(*mask, mw, mh, zoom);

    // One region from all rectangles (much faster than combining one by one)
    std::vector<char> buf(sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT));
    auto* rd = reinterpret_cast<RGNDATA*>(buf.data());
    rd->rdh.dwSize = sizeof(RGNDATAHEADER);
    rd->rdh.iType = RDH_RECTANGLES;
    rd->rdh.nCount = (DWORD)rects.size();
    rd->rdh.nRgnSize = (DWORD)(rects.size() * sizeof(RECT));
    rd->rdh.rcBound = { 0, 0, (LONG)std::lround(mw * zoom), (LONG)std::lround(mh * zoom) };
    RECT* out = reinterpret_cast<RECT*>(rd->Buffer);
    for (size_t i = 0; i < rects.size(); ++i)
        out[i] = { (LONG)rects[i].x0, (LONG)rects[i].y0, (LONG)rects[i].x1, (LONG)rects[i].y1 };
    HRGN rgn = ExtCreateRegion(nullptr, (DWORD)buf.size(), rd);
    if (rgn) SetWindowRgn(hwnd, rgn, TRUE);   // the window owns rgn from now on
}

#elif defined(PULSEAMP_HAVE_XSHAPE)
// X11 Shape extension (also used under XWayland, SDL2's default on Wayland desktops)
void setWindowShape(SDL_Window* win, const std::vector<uint8_t>* mask, int mw, int mh, float zoom) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_X11) return;
    Display* dpy = info.info.x11.display;
    Window   xw  = info.info.x11.window;
    int ev, err;
    if (!XShapeQueryExtension(dpy, &ev, &err)) return;

    if (!mask || mask->size() < (size_t)mw * mh) {
        XShapeCombineMask(dpy, xw, ShapeBounding, 0, 0, None, ShapeSet);   // back to rectangular
        XFlush(dpy);
        return;
    }
    std::vector<Rect> rects = maskToRects(*mask, mw, mh, zoom);
    std::vector<XRectangle> xr;
    xr.reserve(rects.size());
    for (auto& r : rects)
        xr.push_back({ (short)r.x0, (short)r.y0, (unsigned short)(r.x1 - r.x0), (unsigned short)(r.y1 - r.y0) });
    XShapeCombineRectangles(dpy, xw, ShapeBounding, 0, 0, xr.data(), (int)xr.size(), ShapeSet, Unsorted);
    XFlush(dpy);
}

#else
void setWindowShape(SDL_Window*, const std::vector<uint8_t>*, int, int, float) {}
#endif
