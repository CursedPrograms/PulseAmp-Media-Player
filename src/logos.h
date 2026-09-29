#pragma once
// ─── logos.h ──────────────────────────────────────────────────────────────────
// The PulseAmp logos (assets/logo), built into the executable by CMake.
// ─────────────────────────────────────────────────────────────────────────────

struct SDL_Window;

namespace Logos {
    enum Id { Wave, Badge, Wordmark, COUNT };   // logo01, logo02, logo00

    struct Texture { unsigned tex = 0; int w = 0, h = 0; };

    // Decoded and uploaded on first use (GL context must be current); tex = 0 on failure
    const Texture& get(Id id);

    // Delete the textures (GL context must be current)
    void release();

    // Window / taskbar icon from the badge logo. Windows takes it from the exe's
    // icon resource instead, so this only does something elsewhere.
    void setWindowIcon(SDL_Window* win);
}
