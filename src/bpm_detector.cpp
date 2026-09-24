// ─── bpm_detector.cpp ─────────────────────────────────────────────────────────
#include "bpm_detector.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>

BPMDetector::BPMDetector() {
    energy_history_.assign(HISTORY_SIZE, 0.f);
}

float BPMDetector::computeEnergy(const float* buf, int n) {
    float e = 0.f;
    for (int i = 0; i < n; ++i) e += buf[i] * buf[i];
    return e / (float)n;
}

float BPMDetector::computeAvgEnergy() const {
    if (energy_history_.empty()) return 0.f;
    float s = 0.f;
    for (auto v : energy_history_) s += v;
    return s / (float)energy_history_.size();
}

float BPMDetector::feed(const float* mono, int count, int sample_rate) {
    is_beat_ = false;

    int i = 0;
    while (i < count) {
        int chunk = std::min(ENERGY_WINDOW, count - i);
        float e = computeEnergy(mono + i, chunk);
        i += chunk;

        energy_history_.push_back(e);
        if ((int)energy_history_.size() > HISTORY_SIZE)
            energy_history_.pop_front();

        float avg = computeAvgEnergy();
        float variance = 0.f;
        for (auto v : energy_history_)
            variance += (v - avg) * (v - avg);
        variance /= (float)energy_history_.size();

        // Dynamic threshold: higher variance → lower multiplier (busy music)
        float C = (-0.0025714f * variance) + 1.5142857f;
        C = std::clamp(C, 1.1f, 2.0f);

        if (cooldown_ > 0) { cooldown_ -= chunk; continue; }

        if (e > C * avg && avg > 1e-6f) {
            is_beat_ = true;
            int64_t now_ms = (int64_t)((double)sample_count_ / sample_rate * 1000.0);
            beat_times_ms_.push_back(now_ms);
            // Keep only last ~4 seconds of beats
            while (!beat_times_ms_.empty() &&
                   now_ms - beat_times_ms_.front() > 4000)
                beat_times_ms_.pop_front();

            // Estimate BPM from interval between beats
            if (beat_times_ms_.size() >= 4) {
                std::vector<int64_t> intervals;
                for (size_t k = 1; k < beat_times_ms_.size(); ++k)
                    intervals.push_back(beat_times_ms_[k] - beat_times_ms_[k-1]);
                float avg_interval_ms = 0.f;
                for (auto v : intervals) avg_interval_ms += (float)v;
                avg_interval_ms /= (float)intervals.size();
                float new_bpm = 60000.f / avg_interval_ms;
                // Accept only plausible range
                if (new_bpm >= 40.f && new_bpm <= 240.f)
                    bpm_ = bpm_ * 0.85f + new_bpm * 0.15f;
            }
            // 200ms cooldown
            cooldown_ = sample_rate / 5;
        }
        sample_count_ += chunk;
    }
    return bpm_;
}
