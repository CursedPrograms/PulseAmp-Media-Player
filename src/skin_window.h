#pragma once
// ─── skin_window.h ────────────────────────────────────────────────────────────
// Give an SDL window a custom (non-rectangular) shape from a mask, the way
// Winamp's skins did. Windows only; elsewhere the window stays rectangular.
// ─────────────────────────────────────────────────────────────────────────────
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>

// mask: mw*mh bytes (1 = inside), scaled by zoom. mask == nullptr removes the shape.
void setWindowShape(SDL_Window* win, const std::vector<uint8_t>* mask, int mw, int mh, float zoom);
