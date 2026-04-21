// ─── audio_output.cpp ─────────────────────────────────────────────────────────
#include "audio_output.h"
#include <cstring>
#include <iostream>

bool AudioOutput::open(int sample_rate, int channels, AudioRingBuffer* ring) {
    close();
    ring_        = ring;
    sample_rate_ = sample_rate;
    channels_    = channels;

    SDL_AudioSpec desired{}, obtained{};
    desired.freq     = sample_rate_;
    desired.format   = AUDIO_F32SYS;
    desired.channels = (Uint8)channels_;
    desired.samples  = 1024;
    desired.callback = sdlCallback;
    desired.userdata = this;

    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (!device_) {
        std::cerr << "[Audio] SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void AudioOutput::close() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    ring_ = nullptr;
}

void AudioOutput::pause(bool p) {
    if (device_) SDL_PauseAudioDevice(device_, p ? 1 : 0);
}

void AudioOutput::sdlCallback(void* userdata, Uint8* stream, int len) {
    auto* self  = static_cast<AudioOutput*>(userdata);
    int   count = len / sizeof(float);
    auto* dst   = reinterpret_cast<float*>(stream);

    if (self->ring_)
        self->ring_->read(dst, count);
    else
        std::memset(stream, 0, len);
}
