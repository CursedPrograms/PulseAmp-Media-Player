#pragma once
// ─── equalizer.h ──────────────────────────────────────────────────────────────
// 10-band graphic equalizer (peaking biquads, 32 Hz - 16 kHz, +/-12 dB).
// Settings are changed from the UI thread; process() runs in the audio
// callback and picks up changes without locking.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <string>

class Equalizer {
public:
    static constexpr int   BANDS = 10;
    static constexpr float MAX_DB = 12.f;
    static const float     kFreqs[BANDS];

    Equalizer();

    void  setSampleRate(int sr);
    void  setEnabled(bool on)          { enabled_.store(on); }
    bool  enabled() const              { return enabled_.load(); }
    void  setGain(int band, float db);  // switches the preset to "Custom"
    float gain(int band) const         { return gains_[band].load(); }

    // Presets: 0 Flat ... presetCount()-1 Custom
    static int         presetCount();
    static const char* presetName(int i);
    void  setPreset(int i);
    int   preset() const               { return preset_; }

    // "0,1.5,..." <-> gains (for settings.ini)
    std::string gainsString() const;
    void        setGainsString(const std::string& s);

    // Audio thread: interleaved stereo, in place
    void process(float* buf, int frames);

private:
    void design();

    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float z1[2] = {}, z2[2] = {}; };
    Biquad bq_[BANDS];
    float  preamp_ = 1.f;

    std::atomic<bool>  enabled_{ false };
    std::atomic<float> gains_[BANDS];
    std::atomic<int>   version_{ 0 };       // bumped on every change
    int                designed_version_ = -1;
    std::atomic<int>   sample_rate_{ 44100 };
    int                preset_ = 0;
};
