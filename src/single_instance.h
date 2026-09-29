#pragma once
// ─── single_instance.h ────────────────────────────────────────────────────────
// One PulseAmp at a time (Windows): files opened from Explorer while PulseAmp is
// running are handed to the running window instead of opening a new one.
// Elsewhere these do nothing and every launch gets its own window.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <vector>

struct SDL_Window;

namespace SingleInstance {
    // Call before creating the window. If PulseAmp is already running, sends it
    // `files`, brings it to the front and returns true: this process should exit.
    bool forwardToRunning(const std::vector<std::string>& files);

    // Call once the window exists: accept files from later launches
    void listen(SDL_Window* win);

    // Files received since the last call (main thread)
    std::vector<std::string> takeReceived();
}
