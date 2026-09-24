#pragma once
// ─── ui_manager.h ─────────────────────────────────────────────────────────────
// Complete ImGui-based media player interface:
//   • Transport bar (play/pause/stop/next/prev + seek bar + waveform overlay)
//   • Visualizer panel with mode switcher
//   • Tabbed sidebar: Playlist | Converter | Settings | Chapters
//   • Menu bar: File | Playback | View | Tools
//   • Drag-and-drop file opening
// ─────────────────────────────────────────────────────────────────────────────
#include "player.h"
#include "audio_output.h"
#include "video_renderer.h"
#include "visualizer.h"
#include "theme_manager.h"
#include "converter.h"
#include "playlist.h"
#include "bpm_detector.h"
#include "spatial_audio.h"
#include "waveform.h"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <array>
#include <mutex>

class UIManager {
public:
    UIManager(Player& player, AudioOutput& audio, VideoRenderer& video,
              Visualizer& viz, ThemeManager& themes, Converter& conv,
              Playlist& playlist, BPMDetector& bpm, SpatialAudio& spatial,
              WaveformGenerator& waveform);

    // Returns false to signal quit
    bool handleEvent(const SDL_Event& e);
    void render(int window_w, int window_h, double time);

    // Add a file or folder (recursively) to the playlist / start playing an entry
    void addToPlaylist(const std::string& path);
    void playEntry(int idx);

private:
    // ── Sub-panels ────────────────────────────────────────────────────────────
    void drawMenuBar();
    void drawVideoPanel(float x, float y, float w, float h);
    void drawVizPanel(float x, float y, float w, float h, double time);
    void drawTransportBar(float x, float y, float w, float h);
    void drawSidebar(float x, float y, float w, float h, double time);
    void drawPlaylistTab();
    void drawConverterTab();
    void drawSettingsTab();
    void drawChaptersTab();
    void drawInfoOverlay(double time);   // floating metadata overlay

    // ── Helpers ───────────────────────────────────────────────────────────────
    void openFile();
    void openFolder();
    std::string formatTime(double s) const;
    void drawWaveformSeekBar(float x, float y, float w, float h);

    // ── References ────────────────────────────────────────────────────────────
    Player&           player_;
    AudioOutput&      audio_;
    VideoRenderer&    video_;
    Visualizer&       viz_;
    ThemeManager&     themes_;
    Converter&        conv_;
    Playlist&         playlist_;
    BPMDetector&      bpm_;
    SpatialAudio&     spatial_;
    WaveformGenerator& waveform_;

    // ── UI state ──────────────────────────────────────────────────────────────
    bool         show_sidebar_   = true;
    bool         show_viz_       = true;
    bool         show_video_     = true;
    bool         fullscreen_     = false;
    int          sidebar_tab_    = 0;  // 0=playlist 1=convert 2=settings 3=chapters
    bool         info_overlay_   = true;
    float        overlay_alpha_  = 1.f;
    double       overlay_timer_  = 0.0;

    // Converter UI state
    char         conv_output_buf_[512] {};
    int          conv_fmt_idx_   = 0;
    int          conv_audio_kbps_= 192;
    int          conv_vid_kbps_  = 2000;
    bool         conv_strip_vid_ = false;
    bool         conv_strip_aud_ = false;
    ConvertProgress conv_prog_;       // written by the converter thread: guard with conv_mu_
    std::mutex   conv_mu_;
    std::string  conv_source_path_;

    // Waveform data (async-loaded). The generator thread hands results over
    // through waveform_pending_ (guarded by waveform_mu_); render() picks them up.
    std::vector<float> waveform_peaks_;
    bool               waveform_ready_ = false;
    std::string        waveform_path_;
    std::mutex         waveform_mu_;
    std::vector<float> waveform_pending_;
    bool               waveform_pending_ready_ = false;

    // Mood color auto-update
    bool         mood_color_     = false;
    double       mood_timer_     = 0.0;

    // Spatial audio
    float        spatial_width_  = 1.0f;

    // Volume
    float        volume_         = 1.0f;

    // Drag-and-drop
    bool         drag_over_      = false;

    // BPM display
    float        displayed_bpm_  = 0.f;

    // Settings
    bool         smart_resume_   = true;
    bool         auto_advance_   = true;
    float        viz_height_pct_ = 0.25f; // fraction of window height
};
