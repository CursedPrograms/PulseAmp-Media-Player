#pragma once
// ─── bpm_detector.h ───────────────────────────────────────────────────────────
// Simple energy-based beat tracker.  Works well for 60-180 BPM music.
// ─────────────────────────────────────────────────────────────────────────────
#include <deque>
#include <vector>
#include <cstdint>

class BPMDetector {
public:
    BPMDetector();

    // Feed a mono sample buffer.  Returns new BPM estimate (or 0 if not yet known).
    float feed(const float* mono, int count, int sample_rate);

    float getBPM()  const { return bpm_; }
    bool  isBeat()  const { return is_beat_; }   // true on the frame a beat was detected

private:
    static constexpr int ENERGY_WINDOW = 1024;   // samples per energy measurement
    static constexpr int HISTORY_SIZE  = 43;     // ~1 sec at 44100/1024

    float computeEnergy(const float* buf, int n);
    float computeAvgEnergy() const;

    std::deque<float> energy_history_;
    std::deque<int64_t> beat_times_ms_;  // timestamps of recent beats

    int64_t  sample_count_ = 0;
    float    bpm_          = 0.f;
    bool     is_beat_      = false;
    float    beat_thresh_  = 1.5f;  // energy multiplier to declare beat
    int      cooldown_     = 0;     // samples until next beat can fire
};
