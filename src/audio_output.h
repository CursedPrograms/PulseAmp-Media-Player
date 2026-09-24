#pragma once
// ─── audio_output.h ───────────────────────────────────────────────────────────
#include "player.h"
#include "spatial_audio.h"
#include <SDL2/SDL.h>
#include <atomic>

class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput() { close(); }

    bool open(int sample_rate, int channels, AudioRingBuffer* ring);
    void close();

    // Paused: output silence without consuming the buffer (the device keeps
    // running so seeks can still discard stale audio while paused)
    void pause(bool p)                { paused_.store(p); }

    // Applied in the audio callback so changes are heard immediately
    void setGain(float g)             { gain_.store(g); }
    void setSpatial(SpatialAudio* s)  { spatial_ = s; }
    void setSpatialWidth(float w)     { spatial_width_.store(w); }

    int  getSampleRate() const { return sample_rate_; }
    int  getChannels()   const { return channels_; }

private:
    static void sdlCallback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID device_  = 0;
    AudioRingBuffer*  ring_    = nullptr;
    SpatialAudio*     spatial_ = nullptr;
    int               sample_rate_ = 44100;
    int               channels_    = 2;

    std::atomic<bool>  paused_{ false };
    std::atomic<float> gain_{ 1.0f };
    std::atomic<float> spatial_width_{ 1.0f };
};
