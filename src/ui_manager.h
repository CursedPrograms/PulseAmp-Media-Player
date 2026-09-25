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
#include "online.h"
#include "milkdrop.h"
#include "skin.h"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <array>
#include <future>
#include <memory>
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
    ~UIManager();
    bool wantsQuit() const { return quit_requested_; }  // File > Quit

    // Classic mode: compact skinned window (Winamp .wsz or PulseAmp skins)
    bool skinMode() const { return skin_mode_; }
    void toggleSkinMode();
    bool loadSkin(const std::string& path);

    // UI scale factor (window size / DPI); sizes in the layout are multiplied by it
    void  setScale(float s);
    float scale() const { return ui_scale_; }
    void  toggleFullscreen();

    // GL work that must happen before the frame is cleared (MilkDrop renders
    // into the window framebuffer, then copies to a texture). fb_scale: pixels per UI unit.
    void  preRenderGL(float fb_scale);

    // Developer screenshot mode: open a panel ("about", "themes", "settings")
    void  devShow(const std::string& what);

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
    void drawOnlineTab();
    void drawMilkDrop(float x, float y, float w, float h);

    // Classic mode (ui_skin.cpp)
    void drawSkinMode(int win_w, int win_h, double time);
    void drawWinampSkin(double time);
    void drawPulseSkin(double time);
    void drawSkinMenu();
    void drawSkinList();
    bool skinHot(const char* id, float x, float y, float w, float h, bool* pressed);
    void skinAction(const std::string& action);
    bool skinToggleOn(const std::string& action) const;
    std::string skinText(const std::string& what, const std::string& custom) const;
    void applySkinWindow();
    void setSkinZoom(float z);
    void openSkinFile();
    std::vector<std::string> findSkins() const;
    static SDL_HitTestResult skinHitTest(SDL_Window* win, const SDL_Point* pt, void* data);

    // Settings (<appdata>/settings.ini)
    void loadSettings();
    void saveSettings() const;
    void playOnline(const std::string& url, const std::string& title, double dur);
    void onOpened(const PlaylistEntry* pe);   // after the player opened pe
    void pollBackgroundWork();                // online searches / stream opens
    void drawInfoOverlay(double time);   // floating metadata overlay
    void drawAboutWindow();              // Help > About (credits)
    void drawThemeList(bool as_menu);
    void openThemeEditor();
    void drawThemeEditor();
    float S(float px) const { return px * ui_scale_; } // scale a pixel size
    int   vizModeCount() const { return MilkDrop::compiledIn() ? 6 : 5; }

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
    bool         show_about_     = false;
    bool         quit_requested_ = false;
    bool         show_theme_editor_ = false;
    ThemeDef     edit_theme_;
    char         edit_name_buf_[64] {};
    float        ui_scale_       = 1.f;

    // Online (YouTube / SoundCloud via yt-dlp). Streams are resolved and opened
    // on a worker thread, then started on the main thread.
    struct OpenResult {
        std::unique_ptr<PreparedMedia> media;
        ResolvedStream                 rs;
        std::string                    error;
    };
    OnlineService                         online_;
    std::future<OpenResult>               open_job_;
    int                                   open_job_idx_ = -1;
    std::string                           open_job_title_;
    std::vector<std::future<OpenResult>>  stale_jobs_;   // superseded, finishing in background
    std::future<void>                     prefetch_job_; // resolves the next online track early
    std::string                           online_status_;
    int                                   online_src_ = 0;   // 0 YouTube, 1 SoundCloud
    char                                  online_query_[256] {};
    char                                  online_link_[1024] {};
    bool                                  online_video_ = true;
    bool                                  focus_link_   = false;
    double                                search_started_ = 0.0;

    // Drag and drop: files / links from outside, rows inside the app
    int          drop_start_ = -1;          // playlist size when an external drop began
    int          drop_insert_at_ = -1;      // playlist row under the cursor (-1: not over the list)
    bool         drop_on_playlist_ = false;
    ImVec2       pl_list_min_ {0, 0}, pl_list_max_ {0, 0};   // playlist list rect (window coords)
    std::vector<float> pl_row_top_;         // row tops, same coords
    void beginExternalDrop();
    void finishExternalDrop();
    void insertEntries(int first_new, int at);

    // MilkDrop (projectM)
    MilkDrop     milkdrop_;
    float        milkdrop_w_ = 0.f, milkdrop_h_ = 0.f;   // panel size this frame (UI units)
    bool         md_shuffle_ = true, md_locked_ = false;
    float        md_duration_ = 30.f;

    // Classic mode
    std::unique_ptr<Skin> skin_;
    std::string  skin_path_, skin_error_;
    bool         skin_mode_ = false;
    bool         restore_classic_ = false;    // re-enter classic mode on start
    float        skin_zoom_ = 2.f;
    bool         always_on_top_ = false;
    bool         skin_popup_open_ = false;
    int          winamp_vis_ = 0;              // 0 spectrum, 1 oscilloscope, 2 off
    float        skin_seek_preview_ = 0.f;
    ImVec2       skin_o_ {0, 0};
    std::vector<ImVec4> skin_hot_;            // interactive rects (skin px): x, y, w, h
    int          saved_x_ = 0, saved_y_ = 0, saved_w_ = 0, saved_h_ = 0;
    bool         saved_maximized_ = false;
    std::vector<OnlineResult>             online_results_;
    std::string                           online_error_, ytdlp_msg_;
    float        menubar_h_      = 0.f;
    bool         chrome_visible_ = true;   // menu/transport shown (hidden when idle in fullscreen)
    double       last_activity_  = 0.0;   // fullscreen: last mouse movement
    bool         cursor_hidden_  = false;
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
