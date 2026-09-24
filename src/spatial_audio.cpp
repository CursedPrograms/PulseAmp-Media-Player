// ─── spatial_audio.cpp ────────────────────────────────────────────────────────
#include "spatial_audio.h"
#include <cmath>
#include <algorithm>
#include <cstring>

void SpatialAudio::init(int sample_rate) {
    sample_rate_ = sample_rate;
    delay_samples_ = std::clamp(sample_rate / 1000, 1, MAX_DELAY - 1); // ~1ms
    std::memset(delay_buf_L_, 0, sizeof(delay_buf_L_));
    std::memset(delay_buf_R_, 0, sizeof(delay_buf_R_));
    delay_pos_ = 0;
}

void SpatialAudio::process(float* buf, int frames, float width) {
    if (!enabled_) return;

    // width factor: scale mid/side
    // M = (L+R)/2,  S = (L-R)/2
    // L_out = M + S*width, R_out = M - S*width

    for (int i = 0; i < frames; ++i) {
        float L = buf[i*2],   R = buf[i*2+1];
        float M = (L + R) * 0.5f;
        float S = (L - R) * 0.5f * width;

        // Crossfeed: blend delayed opposite channel for headphone naturalness
        int   rd   = (delay_pos_ - delay_samples_ + MAX_DELAY) % MAX_DELAY;
        float delL = delay_buf_L_[rd];
        float delR = delay_buf_R_[rd];
        delay_buf_L_[delay_pos_] = L;
        delay_buf_R_[delay_pos_] = R;
        delay_pos_ = (delay_pos_ + 1) % MAX_DELAY;

        float outL = M + S - delR * crossfeed_gain_;
        float outR = M - S - delL * crossfeed_gain_;
        buf[i*2]   = std::clamp(outL, -1.f, 1.f);
        buf[i*2+1] = std::clamp(outR, -1.f, 1.f);
    }
}
