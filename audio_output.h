#pragma once
// ─── audio_output.h ───────────────────────────────────────────────────────────
#include "player.h"
#include <SDL2/SDL.h>

class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput() { close(); }

    bool open(int sample_rate, int channels, AudioRingBuffer* ring);
    void close();
    void pause(bool p);

    int  getSampleRate() const { return sample_rate_; }
    int  getChannels()   const { return channels_; }

private:
    static void sdlCallback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID device_  = 0;
    AudioRingBuffer*  ring_    = nullptr;
    int               sample_rate_ = 44100;
    int               channels_    = 2;
};
