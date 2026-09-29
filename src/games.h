#pragma once
// ─── games.h ──────────────────────────────────────────────────────────────────
// The arcade: neon Snake, Neon Runner and Invaders for the full window (not
// skins), drawn over the dimmed visualizer and moving with the music: runner
// obstacles arrive on the beat and invaders march to it.
//
// More games are .pag files ("PulseAmp game"): JSON that picks one of the
// built-in engines and sets its rules, colours and levels. They're read from
// games/ next to the exe and <appdata>/games (format: games/README.md).
// ─────────────────────────────────────────────────────────────────────────────
#include <imgui.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A playable game: a built-in, or one loaded from a .pag file
struct GameDef {
    std::string name, engine, author;   // engine: snake | runner | invaders
    std::string path;                   // .pag file ("" = built-in)
    ImU32 c1 = 0, c2 = 0, c3 = 0;       // primary, secondary, accent colours
    float speed   = 1.f;                // overall pace
    bool  beat    = true;               // move with the music
    float gap     = 1.f;                // runner: obstacle spacing
    float gravity = 1.f;                // runner
    int   lives   = 3;                  // invaders
    bool  wrap    = true;               // snake: go through the edges
    std::vector<std::string> map;                  // snake: '#' = wall
    std::vector<std::vector<std::string>> waves;   // invaders: 'S' / 'C' / 'B' = alien
};

// What the music is doing right now
struct GameAudio {
    float energy = 0.f;              // loudness 0..1
    float bass   = 0.f;              // low end 0..1
    bool  beat   = false;            // a beat started this frame
    const float* bars = nullptr;     // spectrum 0..1
    int   nbars  = 0;
};

class GameEngine;

class Arcade {
public:
    Arcade();
    ~Arcade();

    void scan();                     // built-ins + .pag files
    const std::vector<GameDef>&     games()  const { return defs_; }
    const std::vector<std::string>& errors() const { return errors_; }   // .pag files that failed

    void start(int index);
    void stop();
    bool active()  const { return game_ != nullptr; }
    int  current() const { return current_; }

    // One frame (update + draw) in the rectangle p0..p1. Reads the keyboard:
    // arrows / WASD, Space, P = pause, R = restart. The caller handles Esc.
    void frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float energy, float bass,
               const float* bars, int nbars, double time);

private:
    std::vector<GameDef>        defs_;
    std::vector<std::string>    errors_;
    std::unique_ptr<GameEngine> game_;
    int   current_ = -1;
    bool  paused_  = false;
    bool  was_over_ = false;
    float ready_   = 0.f;    // "READY" pause before play starts
    float bass_avg_ = 0.f, since_beat_ = 1.f;
    std::map<std::string, int> best_;   // best score per game name

    void loadBest();
    void saveBest() const;
};
