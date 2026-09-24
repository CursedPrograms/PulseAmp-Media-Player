#pragma once
// ─── visualizer.h ─────────────────────────────────────────────────────────────
// Five OpenGL/ImGui visualizer modes:
//   0 – SpectrumBars   (classic Winamp)
//   1 – Oscilloscope   (waveform trace)
//   2 – RadialSpectrum (circular FFT — exclusive to NovPlayer)
//   3 – BPMPulse       (beat-reactive ring burst — exclusive to NovPlayer)
//   4 – ParticleStorm  (frequency-driven particle field — exclusive to NovPlayer)
// ─────────────────────────────────────────────────────────────────────────────
#include "player.h"
#include <imgui.h>
#include <vector>
#include <complex>
#include <cmath>
#include <random>
#include <array>

// Size of FFT window (power of two)
static constexpr int VIZ_FFT_SIZE = 2048;
static constexpr int VIZ_BARS     = 64;   // display bars

struct VizParticle {
    float x, y;           // position (0..1 normalised)
    float vx, vy;         // velocity
    float life;           // 0=dead, 1=fresh
    float hue;
    float size;
};

class Visualizer {
public:
    enum class Mode { SpectrumBars, Oscilloscope, RadialSpectrum, BPMPulse, ParticleStorm };

    Visualizer();

    // Feed the latest audio samples – call every frame before render()
    void feed(const AudioRingBuffer& ring);

    // Draw inside the current ImGui window using ImDrawList
    void render(ImDrawList* dl, ImVec2 origin, ImVec2 size, float bpm, double time);

    void setMode(Mode m)         { mode_ = m; }
    Mode getMode()         const { return mode_; }

    // Accent colour for visualizer (RGBA u32)
    void setColor(ImU32 col)     { color_ = col; }
    ImU32 getColor()       const { return color_; }

    // Mood: analyzes frequency content, sets a "mood colour"
    ImU32 moodColor() const;

private:
    // ── FFT helpers ────────────────────────────────────────────────────────────
    void computeFFT();
    void fftCooleyTukey(std::vector<std::complex<float>>& x);
    void computeMagnitudes();

    // ── Draw modes ─────────────────────────────────────────────────────────────
    void drawSpectrumBars   (ImDrawList* dl, ImVec2 o, ImVec2 sz, double t);
    void drawOscilloscope   (ImDrawList* dl, ImVec2 o, ImVec2 sz, double t);
    void drawRadialSpectrum (ImDrawList* dl, ImVec2 o, ImVec2 sz, double t);
    void drawBPMPulse       (ImDrawList* dl, ImVec2 o, ImVec2 sz, float bpm, double t);
    void drawParticleStorm  (ImDrawList* dl, ImVec2 o, ImVec2 sz, double t);

    // ── State ──────────────────────────────────────────────────────────────────
    Mode   mode_  = Mode::SpectrumBars;
    ImU32  color_ = IM_COL32(0, 255, 100, 255);

    // Time-domain buffer (latest samples)
    std::array<float, VIZ_FFT_SIZE * 2> time_buf_{}; // interleaved stereo
    // FFT input/output
    std::vector<std::complex<float>> fft_buf_;
    // Smoothed magnitude bars
    std::array<float, VIZ_BARS> bars_{}, bars_peak_{};
    // Raw waveform for oscilloscope
    std::array<float, VIZ_FFT_SIZE> wave_buf_{};

    // Particles for ParticleStorm
    static constexpr int N_PARTICLES = 256;
    std::array<VizParticle, N_PARTICLES> particles_{};
    std::mt19937 rng_;

    // BPM pulse state
    float pulse_radius_ = 0.f;
    float pulse_alpha_  = 0.f;

    double last_time_ = 0.0;
    float  energy_    = 0.f;    // current audio energy (0..1)
    float  bass_      = 0.f;    // low-freq energy
};
