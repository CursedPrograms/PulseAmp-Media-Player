#pragma once
// ─── milkdrop.h ───────────────────────────────────────────────────────────────
// MilkDrop visualizer via projectM (https://github.com/projectM-visualizer).
// Presets (.milk files) are loaded from "presets" next to the executable and
// from <appdata>/presets. Built as a stub when projectM isn't available.
//
// projectM 4.1 always draws to the default framebuffer, so each frame it is
// rendered there first and copied into a texture that the UI shows; the UI is
// drawn over it afterwards (see renderToTexture()).
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <vector>

class AudioRingBuffer;

class MilkDrop {
public:
    MilkDrop() = default;
    ~MilkDrop();
    MilkDrop(const MilkDrop&) = delete;
    MilkDrop& operator=(const MilkDrop&) = delete;

    static bool compiledIn();       // built with projectM?

    // Needs the GL context current. Safe to call repeatedly (initialises once).
    bool init();
    bool ok() const { return handle_ != nullptr; }
    const std::string& error() const { return error_; }

    // Feed the audio that is about to be heard (call once per frame);
    // silence while paused/stopped so the visuals settle down
    void feed(const AudioRingBuffer& ring, float dt, int sample_rate, bool playing);

    // Render a w x h frame into texture(). Uses the window's framebuffer as
    // scratch space: call before clearing it and drawing the UI.
    void renderToTexture(int w, int h);
    unsigned texture() const { return tex_; }

    // Presets
    int  presetCount() const;
    std::string presetName() const;        // current preset (file name)
    void next();
    void previous();
    void random();
    void setShuffle(bool on);
    void setLocked(bool on);
    void setDuration(double seconds);
    void rescanPresets();
    const std::vector<std::string>& presetDirs() const { return dirs_; }

private:
    void* handle_   = nullptr;   // projectm_handle
    void* playlist_ = nullptr;   // projectm_playlist_handle
    bool  tried_    = false;
    std::string error_;
    std::vector<std::string> dirs_;
    unsigned tex_ = 0;
    int tex_w_ = 0, tex_h_ = 0;
    int pm_w_ = 0, pm_h_ = 0;
    std::vector<float> pcm_;
};
