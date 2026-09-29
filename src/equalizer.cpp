// ─── equalizer.cpp ────────────────────────────────────────────────────────────
#include "equalizer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

const float Equalizer::kFreqs[BANDS] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

namespace {
struct Preset { const char* name; float db[Equalizer::BANDS]; };
const Preset kPresets[] = {
    { "Flat",        {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 } },
    { "Bass Boost",  {  6,  5,  4,  2,  0,  0,  0,  0,  0,  0 } },
    { "Treble",      {  0,  0,  0,  0,  0,  1,  2,  4,  5,  6 } },
    { "Vocal",       { -2, -1,  0,  2,  4,  4,  3,  1,  0, -1 } },
    { "Rock",        {  5,  3, -1, -2, -1,  1,  3,  4,  4,  4 } },
    { "Electronic",  {  5,  4,  1,  0, -2,  1,  0,  2,  4,  5 } },
    { "Pop",         { -1,  1,  3,  4,  3,  0, -1, -1, -1, -1 } },
    { "Custom",      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 } },
};
constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
}

Equalizer::Equalizer() {
    for (auto& g : gains_) g.store(0.f);
}

int         Equalizer::presetCount()      { return kPresetCount; }
const char* Equalizer::presetName(int i)  { return kPresets[std::clamp(i, 0, kPresetCount - 1)].name; }

void Equalizer::setSampleRate(int sr) {
    sample_rate_.store(sr);
    version_.fetch_add(1);
}

void Equalizer::setGain(int band, float db) {
    if (band < 0 || band >= BANDS) return;
    gains_[band].store(std::clamp(db, -MAX_DB, MAX_DB));
    preset_ = kPresetCount - 1;   // Custom
    version_.fetch_add(1);
}

void Equalizer::setPreset(int i) {
    i = std::clamp(i, 0, kPresetCount - 1);
    preset_ = i;
    if (i != kPresetCount - 1)    // Custom keeps the current gains
        for (int b = 0; b < BANDS; ++b) gains_[b].store(kPresets[i].db[b]);
    version_.fetch_add(1);
}

std::string Equalizer::gainsString() const {
    std::string s;
    char buf[16];
    for (int b = 0; b < BANDS; ++b) {
        std::snprintf(buf, sizeof(buf), b ? ",%.1f" : "%.1f", gains_[b].load());
        s += buf;
    }
    return s;
}

void Equalizer::setGainsString(const std::string& s) {
    const char* p = s.c_str();
    for (int b = 0; b < BANDS && *p; ++b) {
        char* end = nullptr;
        float v = std::strtof(p, &end);
        if (end == p) break;
        gains_[b].store(std::clamp(v, -MAX_DB, MAX_DB));
        p = *end == ',' ? end + 1 : end;
    }
    version_.fetch_add(1);
}

// RBJ "Audio EQ Cookbook" peaking filters
void Equalizer::design() {
    const float sr = (float)sample_rate_.load();
    const float Q = 1.1f;
    float max_boost = 0.f;
    for (int b = 0; b < BANDS; ++b) {
        const float g = gains_[b].load();
        max_boost = std::max(max_boost, g);
        const float f = std::min(kFreqs[b], sr * 0.45f);
        const float A = std::pow(10.f, g / 40.f);
        const float w0 = 2.f * 3.14159265f * f / sr;
        const float cs = std::cos(w0), alpha = std::sin(w0) / (2.f * Q);
        const float a0 = 1.f + alpha / A;
        Biquad& q = bq_[b];
        q.b0 = (1.f + alpha * A) / a0;
        q.b1 = (-2.f * cs) / a0;
        q.b2 = (1.f - alpha * A) / a0;
        q.a1 = (-2.f * cs) / a0;
        q.a2 = (1.f - alpha / A) / a0;
    }
    // Leave headroom for boosts so the output doesn't clip
    preamp_ = std::pow(10.f, -max_boost * 0.7f / 20.f);
}

void Equalizer::process(float* buf, int frames) {
    if (!enabled_.load()) return;
    const int v = version_.load();
    if (v != designed_version_) { design(); designed_version_ = v; }

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float x = buf[i * 2 + ch] * preamp_;
            for (auto& q : bq_) {
                // Transposed direct form II
                float y = q.b0 * x + q.z1[ch];
                q.z1[ch] = q.b1 * x - q.a1 * y + q.z2[ch];
                q.z2[ch] = q.b2 * x - q.a2 * y;
                x = y;
            }
            buf[i * 2 + ch] = std::clamp(x, -1.f, 1.f);
        }
    }
}
