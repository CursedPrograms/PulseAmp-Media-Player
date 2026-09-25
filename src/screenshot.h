#pragma once
// ─── screenshot.h ─────────────────────────────────────────────────────────────
// Developer aid: render the UI offscreen and save it as a PNG.
//   PulseAmp --screenshot out.png [--size 1280x780] [--frames 30] [--show about|themes|settings]
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

// Save the RGBA contents of the currently bound framebuffer (w x h) as PNG
bool saveFramebufferPNG(const std::string& path, int w, int h);
