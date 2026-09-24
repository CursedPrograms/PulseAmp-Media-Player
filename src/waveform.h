#pragma once
// ─── waveform.h ───────────────────────────────────────────────────────────────
// Generates a downsampled RMS waveform overview for the entire file.
// Used in the seek bar as a waveform scrubber — a feature rare in media players.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

class WaveformGenerator {
public:
    WaveformGenerator();
    ~WaveformGenerator();

    // Start async waveform generation for a file.
    // Calls callback(peaks) when done; peaks[i] is RMS in 0..1
    void generate(const std::string& path, int num_buckets,
                  std::function<void(std::vector<float>)> callback);

    // Cancel any running generation
    void cancel();

    bool isRunning() const { return running_.load(); }

private:
    void generateThread(std::string path, int num_buckets,
                        std::function<void(std::vector<float>)> callback);

    std::thread           thread_;
    std::atomic<bool>     running_{ false };
    std::atomic<bool>     cancel_ { false };
};
