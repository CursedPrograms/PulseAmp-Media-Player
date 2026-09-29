// ─── single_instance.cpp ──────────────────────────────────────────────────────
#include "single_instance.h"

#ifdef _WIN32
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>

namespace {

constexpr ULONG_PTR COPYDATA_FILES = 0x504D4150;   // 'PAMP'
constexpr const char* WINDOW_PROP  = "PulseAmp.MainWindow";

WNDPROC                  g_prev_proc = nullptr;
std::vector<std::string> g_received;   // filled on the main thread (inside the event pump)

// WM_COPYDATA from a second launch: a list of '\0'-terminated UTF-8 paths.
// The data is only valid during this call, so it's copied out right away.
LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COPYDATA) {
        const auto* cd = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if (cd && cd->dwData == COPYDATA_FILES) {
            const char* p   = static_cast<const char*>(cd->lpData);
            const char* end = p + (p ? cd->cbData : 0);
            while (p < end) {
                const size_t n = strnlen(p, (size_t)(end - p));
                if (n) g_received.emplace_back(p, n);
                p += n + 1;
            }
            return TRUE;
        }
    }
    return CallWindowProcW(g_prev_proc, h, m, w, l);
}

BOOL CALLBACK findMainWindow(HWND h, LPARAM out) {
    if (GetPropA(h, WINDOW_PROP)) { *reinterpret_cast<HWND*>(out) = h; return FALSE; }
    return TRUE;
}

HWND runningWindow() {
    HWND found = nullptr;
    EnumWindows(findMainWindow, reinterpret_cast<LPARAM>(&found));
    return found;
}

} // namespace

bool SingleInstance::forwardToRunning(const std::vector<std::string>& files) {
    // The mutex tells us whether another PulseAmp exists at all. Kept open for
    // the life of the process (the OS closes it on exit).
    CreateMutexA(nullptr, FALSE, "Local\\PulseAmp.SingleInstance");
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;

    // It may still be starting up (e.g. several files opened at once): wait for its window
    HWND target = nullptr;
    for (int i = 0; i < 100 && !(target = runningWindow()); ++i) Sleep(50);
    if (!target) return false;

    // We were launched by the user, so we may give the focus to the running window
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);

    if (files.empty()) return true;
    std::string data;
    for (const auto& f : files) { data += f; data += '\0'; }
    COPYDATASTRUCT cd{};
    cd.dwData = COPYDATA_FILES;
    cd.cbData = (DWORD)data.size();
    cd.lpData = data.data();
    DWORD_PTR result = 0;
    return SendMessageTimeoutA(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd),
                               SMTO_ABORTIFHUNG, 5000, &result) && result;
}

void SingleInstance::listen(SDL_Window* win) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    HWND hwnd = info.info.win.window;
    g_prev_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProc)));
    SetPropA(hwnd, WINDOW_PROP, reinterpret_cast<HANDLE>(1));
}

std::vector<std::string> SingleInstance::takeReceived() {
    std::vector<std::string> out;
    out.swap(g_received);
    return out;
}

#else

bool SingleInstance::forwardToRunning(const std::vector<std::string>&) { return false; }
void SingleInstance::listen(SDL_Window*) {}
std::vector<std::string> SingleInstance::takeReceived() { return {}; }

#endif
