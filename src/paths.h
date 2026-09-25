#pragma once
// ─── paths.h ──────────────────────────────────────────────────────────────────
// Per-user data locations:
//   Windows:     %APPDATA%\PulseAmp
//   Linux/macOS: ~/.pulseamp
//   Override:    PULSEAMP_DATA_DIR environment variable
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

// Directory for settings, themes, resume data (created on first use)
std::string appDataDir();

// appDataDir() + separator + name
std::string appDataPath(const std::string& name);

// Directory containing the running executable (for bundled tools/skins/presets)
std::string exeDir();
