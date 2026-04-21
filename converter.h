#pragma once
// ─── converter.h ──────────────────────────────────────────────────────────────
// FFmpeg-based format converter.  Runs in a background thread.
// Supports: mp3, mp4, mkv, avi, wav, flac, ogg, aac, opus, webm
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

struct ConvertJob {
    std::string input_path;
    std::string output_path;
    std::string output_format;  // e.g. "mp3", "flac", "mp4"
    int         audio_bitrate   = 192000; // bps
    int         video_bitrate   = 2000000;
    int         width           = 0;  // 0 = keep source
    int         height          = 0;
    bool        strip_video     = false; // audio-only output
    bool        strip_audio     = false; // video-only output
};

struct ConvertProgress {
    double  progress     = 0.0;  // 0..1
    double  duration     = 0.0;
    double  current_time = 0.0;
    bool    done         = false;
    bool    error        = false;
    std::string error_msg;
};

class Converter {
public:
    Converter();
    ~Converter() { cancel(); }

    // Start conversion.  progress_cb is called from worker thread.
    void start(const ConvertJob& job,
               std::function<void(ConvertProgress)> progress_cb);

    void cancel();
    bool isRunning() const { return running_.load(); }

    // Convenience: list available output formats for the UI
    static std::vector<std::string> availableFormats();

private:
    void convertThread(ConvertJob job,
                       std::function<void(ConvertProgress)> cb);

    std::thread       thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> cancel_ { false };
};
