#pragma once
// ─── spatial_audio.h ──────────────────────────────────────────────────────────
// Haas-effect stereo widener + simple cross-feed filter.
// Applied in-place on interleaved stereo float buffers.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <atomic>

class SpatialAudio {
public:
    SpatialAudio() = default;
    void init(int sample_rate);

    // Process interleaved stereo buffer in-place
    // width: 0 = mono, 1 = normal stereo, 2 = extreme wide
    void process(float* buf, int frames, float width);

    void setEnabled(bool e) { enabled_.store(e); }
    bool isEnabled()  const { return enabled_.load(); }

private:
    static constexpr int MAX_DELAY = 48; // samples (~1ms at 48kHz)
    float delay_buf_L_[MAX_DELAY]{}, delay_buf_R_[MAX_DELAY]{};
    int   delay_pos_ = 0;
    int   delay_samples_ = 18;
    float crossfeed_gain_ = 0.3f;
    std::atomic<bool> enabled_{ false }; // toggled by the UI, read by the audio thread
    int   sample_rate_ = 44100;
};
