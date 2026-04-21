// ─── visualizer.cpp ───────────────────────────────────────────────────────────
#include "visualizer.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Visualizer::Visualizer() : rng_(std::random_device{}()) {
    fft_buf_.resize(VIZ_FFT_SIZE);
    bars_.fill(0.f); bars_peak_.fill(0.f); wave_buf_.fill(0.f);
    // Initialise particles as dead
    for (auto& p : particles_) p.life = 0.f;
}

// ─── Feed ─────────────────────────────────────────────────────────────────────
void Visualizer::feed(const AudioRingBuffer& ring) {
    ring.peekLatest(time_buf_.data(), VIZ_FFT_SIZE);
    // Copy to wave buf (mono mix L+R)
    for (int i = 0; i < VIZ_FFT_SIZE / 2; ++i)
        wave_buf_[i] = (time_buf_[i * 2] + time_buf_[i * 2 + 1]) * 0.5f;

    computeFFT();
    computeMagnitudes();

    // Compute overall energy
    float sum = 0.f;
    for (auto v : bars_) sum += v;
    energy_ = std::min(1.f, sum / VIZ_BARS);
    // Bass = first 4 bars
    bass_ = std::min(1.f, (bars_[0]+bars_[1]+bars_[2]+bars_[3]) * 0.5f);
}

// ─── Cooley-Tukey FFT ─────────────────────────────────────────────────────────
void Visualizer::fftCooleyTukey(std::vector<std::complex<float>>& x) {
    int N = (int)x.size();
    if (N <= 1) return;
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    // FFT butterfly
    for (int len = 2; len <= N; len <<= 1) {
        float ang = -2.f * (float)M_PI / (float)len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < N; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int j = 0; j < len / 2; ++j) {
                auto u = x[i + j];
                auto v = x[i + j + len/2] * w;
                x[i + j]         = u + v;
                x[i + j + len/2] = u - v;
                w *= wlen;
            }
        }
    }
}

void Visualizer::computeFFT() {
    // Fill FFT buffer with mono samples * Hann window
    for (int i = 0; i < VIZ_FFT_SIZE; ++i) {
        float hann = 0.5f * (1.f - std::cos(2.f*(float)M_PI*i / (VIZ_FFT_SIZE - 1)));
        float mono = (time_buf_[i*2 % VIZ_FFT_SIZE] + time_buf_[(i*2+1) % VIZ_FFT_SIZE]) * 0.5f;
        fft_buf_[i] = {mono * hann, 0.f};
    }
    fftCooleyTukey(fft_buf_);
}

void Visualizer::computeMagnitudes() {
    // Map FFT bins → VIZ_BARS on a log scale
    int half = VIZ_FFT_SIZE / 2;
    float lo = std::log2f(1.f), hi = std::log2f((float)half);
    float range = hi - lo;
    for (int b = 0; b < VIZ_BARS; ++b) {
        float f0 = lo + (float)b     / VIZ_BARS * range;
        float f1 = lo + (float)(b+1) / VIZ_BARS * range;
        int bin0 = std::max(1, (int)std::pow(2.f, f0));
        int bin1 = std::min(half - 1, (int)std::pow(2.f, f1));
        if (bin0 >= bin1) bin1 = bin0 + 1;
        float mag = 0.f;
        for (int k = bin0; k <= bin1; ++k) {
            float re = fft_buf_[k].real(), im = fft_buf_[k].imag();
            mag = std::max(mag, std::sqrt(re*re + im*im));
        }
        // Convert to dB, normalise 0..1
        float db = 20.f * std::log10f(std::max(mag, 1e-6f));
        float norm = (db + 70.f) / 70.f; // -70 dB → 0,  0 dB → 1
        norm = std::clamp(norm, 0.f, 1.f);
        // Smooth (attack fast, decay slow)
        float alpha = norm > bars_[b] ? 0.6f : 0.15f;
        bars_[b] = bars_[b] * (1.f - alpha) + norm * alpha;
        // Peak hold
        if (bars_[b] > bars_peak_[b]) bars_peak_[b] = bars_[b];
        else                           bars_peak_[b] *= 0.99f;
    }
}

// ─── Render dispatcher ────────────────────────────────────────────────────────
void Visualizer::render(ImDrawList* dl, ImVec2 origin, ImVec2 size, float bpm, double time) {
    double dt = time - last_time_;
    last_time_ = time;
    (void)dt;

    switch (mode_) {
        case Mode::SpectrumBars:    drawSpectrumBars  (dl, origin, size, time); break;
        case Mode::Oscilloscope:    drawOscilloscope  (dl, origin, size, time); break;
        case Mode::RadialSpectrum:  drawRadialSpectrum(dl, origin, size, time); break;
        case Mode::BPMPulse:        drawBPMPulse      (dl, origin, size, bpm,  time); break;
        case Mode::ParticleStorm:   drawParticleStorm (dl, origin, size, time); break;
    }
}

// ─── Mode 0: Spectrum Bars (classic Winamp) ───────────────────────────────────
void Visualizer::drawSpectrumBars(ImDrawList* dl, ImVec2 o, ImVec2 sz, double /*t*/) {
    float barW = sz.x / VIZ_BARS;
    ImU32 col  = color_;

    for (int b = 0; b < VIZ_BARS; ++b) {
        float h   = bars_[b] * sz.y;
        float x0  = o.x + b * barW + 1.f;
        float x1  = o.x + (b+1) * barW - 1.f;
        float y0  = o.y + sz.y - h;
        float y1  = o.y + sz.y;

        // Gradient: accent → darker at base
        ImU32 topCol = col;
        ImU32 botCol = IM_COL32(
            (col & 0xFF) / 3,
            ((col >> 8) & 0xFF) / 3,
            ((col >> 16) & 0xFF) / 3,
            255);
        dl->AddRectFilledMultiColor({x0,y0},{x1,y1}, topCol,topCol, botCol,botCol);

        // Peak dot
        float ph = bars_peak_[b] * sz.y;
        float py = o.y + sz.y - ph - 2.f;
        dl->AddRectFilled({x0, py}, {x1, py+2.f}, IM_COL32(255,255,255,180));
    }
}

// ─── Mode 1: Oscilloscope ─────────────────────────────────────────────────────
void Visualizer::drawOscilloscope(ImDrawList* dl, ImVec2 o, ImVec2 sz, double /*t*/) {
    int N = VIZ_FFT_SIZE / 2;
    float cx = o.x + sz.x * 0.5f, cy = o.y + sz.y * 0.5f;
    ImU32 col = color_;

    // Draw centre line
    dl->AddLine({o.x, cy}, {o.x+sz.x, cy}, IM_COL32(60,60,60,200));

    for (int i = 1; i < N; ++i) {
        float x0 = o.x + (float)(i-1) / (N-1) * sz.x;
        float y0 = cy  - wave_buf_[i-1] * sz.y * 0.45f;
        float x1 = o.x + (float)i       / (N-1) * sz.x;
        float y1 = cy  - wave_buf_[i]   * sz.y * 0.45f;
        // Colour by amplitude
        float amp = std::abs(wave_buf_[i]);
        ImU32 c = IM_COL32(
            std::min(255,(int)(((col>>IM_COL32_R_SHIFT)&0xFF) * (0.4f + amp*1.6f))),
            std::min(255,(int)(((col>>IM_COL32_G_SHIFT)&0xFF) * (0.4f + amp*1.6f))),
            std::min(255,(int)(((col>>IM_COL32_B_SHIFT)&0xFF) * (0.4f + amp*1.6f))),
            220);
        dl->AddLine({x0,y0},{x1,y1}, c, 1.5f);
    }
}

// ─── Mode 2: Radial Spectrum (NovPlayer exclusive) ────────────────────────────
void Visualizer::drawRadialSpectrum(ImDrawList* dl, ImVec2 o, ImVec2 sz, double t) {
    float cx = o.x + sz.x * 0.5f;
    float cy = o.y + sz.y * 0.5f;
    float R  = std::min(sz.x, sz.y) * 0.38f;

    // Rotating ring
    for (int b = 0; b < VIZ_BARS; ++b) {
        float angle = (float)b / VIZ_BARS * 2.f*(float)M_PI + (float)t * 0.3f;
        float mag   = bars_[b] * R * 0.8f;
        float x0 = cx + std::cos(angle) * (R + 4.f);
        float y0 = cy + std::sin(angle) * (R + 4.f);
        float x1 = cx + std::cos(angle) * (R + 4.f + mag);
        float y1 = cy + std::sin(angle) * (R + 4.f + mag);

        float hue = (float)b / VIZ_BARS + (float)t * 0.05f;
        hue -= std::floor(hue);
        // HSV→RGB simple
        int hi = (int)(hue * 6.f) % 6;
        float f = hue*6.f - std::floor(hue*6.f);
        float q = 1.f - f, t2 = f;
        float r,g,bl;
        switch(hi){
            case 0: r=1,g=t2,bl=0; break;
            case 1: r=q,g=1,bl=0; break;
            case 2: r=0,g=1,bl=t2; break;
            case 3: r=0,g=q,bl=1; break;
            case 4: r=t2,g=0,bl=1; break;
            default:r=1,g=0,bl=q; break;
        }
        ImU32 c = IM_COL32((int)(r*255),(int)(g*255),(int)(bl*255),220);
        dl->AddLine({x0,y0},{x1,y1}, c, 2.5f);
    }

    // Centre glow disc pulsing with energy
    float gr = R * 0.25f * (1.f + energy_ * 0.5f);
    dl->AddCircleFilled({cx,cy}, gr,
        IM_COL32((color_>>IM_COL32_R_SHIFT)&0xFF,
                 (color_>>IM_COL32_G_SHIFT)&0xFF,
                 (color_>>IM_COL32_B_SHIFT)&0xFF,
                 (int)(100 + energy_*120)), 48);
}

// ─── Mode 3: BPM Pulse (NovPlayer exclusive) ──────────────────────────────────
void Visualizer::drawBPMPulse(ImDrawList* dl, ImVec2 o, ImVec2 sz, float bpm, double t) {
    float cx = o.x + sz.x*0.5f, cy = o.y + sz.y*0.5f;
    float maxR = std::min(sz.x, sz.y) * 0.48f;

    // Beat period
    double period = (bpm > 1.f) ? 60.0 / bpm : 0.5;
    double phase  = std::fmod(t, period) / period; // 0..1

    // Draw N expanding rings, each offset by 1/3 period
    for (int ring = 0; ring < 3; ++ring) {
        double ph = std::fmod(phase + (double)ring / 3.0, 1.0);
        float  r  = (float)ph * maxR;
        float  a  = 1.f - (float)ph;
        a *= (0.5f + bass_ * 0.5f);

        ImU32 c = IM_COL32(
            (color_>>IM_COL32_R_SHIFT)&0xFF,
            (color_>>IM_COL32_G_SHIFT)&0xFF,
            (color_>>IM_COL32_B_SHIFT)&0xFF,
            (int)(a*200));
        dl->AddCircle({cx,cy}, r, c, 64, 2.f + (1.f-(float)ph)*3.f);
    }

    // Bass-reactive filled core
    float coreR = maxR * 0.12f * (1.f + bass_ * 2.f);
    dl->AddCircleFilled({cx,cy}, coreR,
        IM_COL32((color_>>IM_COL32_R_SHIFT)&0xFF,
                 (color_>>IM_COL32_G_SHIFT)&0xFF,
                 (color_>>IM_COL32_B_SHIFT)&0xFF,
                 (int)(100 + bass_*155)), 48);

    // Mini spectrum ring border
    for (int b = 0; b < VIZ_BARS; ++b) {
        float angle = (float)b / VIZ_BARS * 2.f*(float)M_PI;
        float inner = maxR * 0.55f;
        float outer = inner + bars_[b] * maxR * 0.35f;
        float x0 = cx + std::cos(angle)*inner, y0 = cy + std::sin(angle)*inner;
        float x1 = cx + std::cos(angle)*outer, y1 = cy + std::sin(angle)*outer;
        dl->AddLine({x0,y0},{x1,y1}, color_, 1.5f);
    }
}

// ─── Mode 4: Particle Storm (NovPlayer exclusive) ─────────────────────────────
void Visualizer::drawParticleStorm(ImDrawList* dl, ImVec2 o, ImVec2 sz, double t) {
    std::uniform_real_distribution<float> posD(0.f, 1.f);
    std::uniform_real_distribution<float> velD(-0.01f, 0.01f);
    std::uniform_real_distribution<float> hueD(0.f, 1.f);

    float dt = 0.016f; // assume 60fps
    float cx = 0.5f, cy = 0.5f;

    // Spawn new particles proportional to energy
    int spawn = (int)(energy_ * 8.f);
    for (int i = 0; i < spawn; ++i) {
        // Find dead particle
        for (auto& p : particles_) {
            if (p.life <= 0.f) {
                p.x    = cx + posD(rng_) * 0.1f - 0.05f;
                p.y    = cy + posD(rng_) * 0.1f - 0.05f;
                p.vx   = velD(rng_) * (1.f + energy_);
                p.vy   = velD(rng_) * (1.f + energy_) - 0.002f; // slight upward drift
                p.life = 0.8f + posD(rng_) * 0.2f;
                p.hue  = hueD(rng_);
                p.size = 2.f + energy_ * 5.f;
                break;
            }
        }
    }

    // Update + draw
    for (auto& p : particles_) {
        if (p.life <= 0.f) continue;
        p.life -= dt * 0.4f;
        p.vx   *= 0.98f;
        p.vy   *= 0.98f;
        p.vy   -= 0.0005f; // drift up
        p.x    += p.vx;
        p.y    += p.vy;

        // Screen coords
        float px = o.x + p.x * sz.x;
        float py = o.y + p.y * sz.y;
        if (px < o.x || px > o.x+sz.x || py < o.y || py > o.y+sz.y) {
            p.life = 0.f; continue;
        }

        float hue = p.hue + (float)t * 0.05f; hue -= std::floor(hue);
        int hi = (int)(hue*6.f)%6; float f=hue*6.f-std::floor(hue*6.f);
        float q=1.f-f,tf=f,r,g,bl;
        switch(hi){case 0:r=1,g=tf,bl=0;break;case 1:r=q,g=1,bl=0;break;
                   case 2:r=0,g=1,bl=tf;break;case 3:r=0,g=q,bl=1;break;
                   case 4:r=tf,g=0,bl=1;break;default:r=1,g=0,bl=q;break;}
        ImU32 c = IM_COL32((int)(r*255),(int)(g*255),(int)(bl*255),(int)(p.life*230));
        float s = p.size * p.life;
        dl->AddCircleFilled({px,py}, s, c, 8);
    }
}

// ─── Mood color ───────────────────────────────────────────────────────────────
ImU32 Visualizer::moodColor() const {
    // Analyze frequency balance: bass-heavy = warm red, treble-heavy = cool blue
    float bassSum = 0.f, midSum = 0.f, trebleSum = 0.f;
    for (int i = 0;              i < VIZ_BARS/4;    ++i) bassSum   += bars_[i];
    for (int i = VIZ_BARS/4;    i < VIZ_BARS*3/4;  ++i) midSum    += bars_[i];
    for (int i = VIZ_BARS*3/4;  i < VIZ_BARS;      ++i) trebleSum += bars_[i];
    float total = bassSum + midSum + trebleSum + 0.001f;
    float bassR = bassSum / total, midR = midSum / total, trebleR = trebleSum / total;

    // Bass → red/orange, mid → green, treble → blue/cyan
    int r = (int)(bassR * 255 * 2);
    int g = (int)(midR  * 255 * 1.5f);
    int b = (int)(trebleR * 255 * 2);
    return IM_COL32(std::min(255,r), std::min(255,g), std::min(255,b), 255);
}
