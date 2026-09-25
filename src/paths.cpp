// ─── paths.cpp ────────────────────────────────────────────────────────────────
#include "paths.h"
#include <SDL2/SDL.h>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

std::string appDataDir() {
    static std::string dir = [] {
        // Override (portable installs, testing)
        if (const char* over = std::getenv("PULSEAMP_DATA_DIR"); over && *over) {
            std::error_code ec;
            fs::create_directories(over, ec);
            return std::string(over);
        }
#ifdef _WIN32
        const char* base = std::getenv("APPDATA");
        fs::path p = base ? fs::path(base) / "PulseAmp" : fs::path("PulseAmp");
#else
        const char* home = std::getenv("HOME");
        fs::path p = home ? fs::path(home) / ".pulseamp" : fs::path(".pulseamp");
#endif
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.string();
    }();
    return dir;
}

std::string appDataPath(const std::string& name) {
    return (fs::path(appDataDir()) / name).string();
}

std::string exeDir() {
    static std::string dir = [] {
        char* base = SDL_GetBasePath();
        std::string d = base ? base : "";
        SDL_free(base);
        return d;
    }();
    return dir;
}
