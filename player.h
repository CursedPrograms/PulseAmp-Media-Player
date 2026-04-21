#pragma once
// ─── player.h  ────────────────────────────────────────────────────────────────
// Thread-safe FFmpeg decode pipeline.
// Demux → [audio thread] → AudioRingBuffer  → SDL2 audio callback
//       → [video thread] → VideoFrameQueue  → render thread
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <cstdint>
#include <cassert>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// ─── Video frame (RGBA, ready for upload to GL texture) ──────────────────────
struct VideoFrame {
    std::vector<uint8_t> rgba;   // width * height * 4
    int   width  = 0;
    int   height = 0;
    double pts   = 0.0;          // presentation timestamp (seconds)
};

// ─── Player state ─────────────────────────────────────────────────────────────
enum class PlayerState { Stopped, Opening, Playing, Paused };

// ─── Chapter info ─────────────────────────────────────────────────────────────
struct Chapter {
    std::string title;
    double      start = 0.0; // seconds
    double      end   = 0.0;
};

// ─── Audio ring buffer (float32 stereo, lock-free single-producer/consumer) ───
class AudioRingBuffer {
public:
    static constexpr int CAPACITY = 44100 * 2 * 4; // ~4 s at 44100 Hz stereo
    explicit AudioRingBuffer() : buf_(CAPACITY, 0.f) {}

    // Producer – called from audio decode thread
    int write(const float* src, int count);
    // Consumer – called from SDL audio callback
    int read(float* dst, int count);
    // Visualizer tap (thread-safe snapshot of last N samples)
    void peekLatest(float* dst, int count) const;

    void reset();
    int available() const { return count_.load(std::memory_order_acquire); }

private:
    std::vector<float>  buf_;
    std::atomic<int>    write_{ 0 };
    std::atomic<int>    read_ { 0 };
    std::atomic<int>    count_{ 0 };
};

// ─── Player ───────────────────────────────────────────────────────────────────
class Player {
public:
    Player();
    ~Player();

    // Non-copyable / non-movable
    Player(const Player&)            = delete;
    Player& operator=(const Player&) = delete;

    // ── Playback control ──────────────────────────────────────────────────────
    bool        open(const std::string& path);
    void        close();
    void        play();
    void        pause();
    void        stop();
    void        seek(double seconds);
    void        setVolume(float v) { volume_.store(v); }  // 0..1
    float       getVolume()  const { return volume_.load(); }
    void        setMuted(bool m)   { muted_.store(m); }
    bool        isMuted()    const { return muted_.load(); }

    // ── State accessors ───────────────────────────────────────────────────────
    PlayerState         getState()       const { return state_.load(); }
    double              getDuration()    const { return duration_; }
    double              getCurrentTime() const { return current_time_.load(); }
    bool                hasVideo()       const { return video_stream_ >= 0; }
    bool                hasAudio()       const { return audio_stream_ >= 0; }
    int                 getVideoWidth()  const { return video_width_; }
    int                 getVideoHeight() const { return video_height_; }
    int                 getSampleRate()  const { return out_sample_rate_; }
    int                 getChannels()    const { return 2; }
    const std::string&  getFilePath()    const { return file_path_; }

    // ── Chapters ──────────────────────────────────────────────────────────────
    const std::vector<Chapter>& getChapters() const { return chapters_; }

    // ── Frame / sample access ─────────────────────────────────────────────────
    std::shared_ptr<VideoFrame> pollVideoFrame(); // nullptr if none ready
    AudioRingBuffer& getAudioBuffer() { return audio_ring_; }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void setEndCallback(std::function<void()> cb) { end_cb_ = std::move(cb); }

private:
    void demuxLoop();
    void audioDecodeLoop();
    void videoDecodeLoop();
    void parseChapters();

    // ── FFmpeg state ──────────────────────────────────────────────────────────
    AVFormatContext*  fmt_ctx_   = nullptr;
    AVCodecContext*   audio_ctx_ = nullptr;
    AVCodecContext*   video_ctx_ = nullptr;
    SwrContext*       swr_ctx_   = nullptr;
    SwsContext*       sws_ctx_   = nullptr;
    int               audio_stream_ = -1;
    int               video_stream_ = -1;

    double duration_      = 0.0;
    int    video_width_   = 0;
    int    video_height_  = 0;
    int    out_sample_rate_ = 44100;

    std::atomic<PlayerState> state_{ PlayerState::Stopped };
    std::atomic<double>      current_time_{ 0.0 };
    std::atomic<float>       volume_{ 1.0f };
    std::atomic<bool>        muted_{ false };
    std::atomic<bool>        seek_requested_{ false };
    std::atomic<double>      seek_target_{ 0.0 };
    std::atomic<bool>        running_{ false };

    // ── Packet queues (demux → decode threads) ────────────────────────────────
    static constexpr int MAX_AUDIO_PKTS = 128;
    static constexpr int MAX_VIDEO_PKTS = 32;

    std::queue<AVPacket*>   audio_pkts_, video_pkts_;
    std::mutex              audio_pkt_mu_, video_pkt_mu_;
    std::condition_variable audio_pkt_cv_, video_pkt_cv_;

    // ── Video frame queue ─────────────────────────────────────────────────────
    static constexpr int MAX_VIDEO_FRAMES = 4;
    std::queue<std::shared_ptr<VideoFrame>> video_frames_;
    std::mutex                              video_frame_mu_;
    std::condition_variable                 video_frame_cv_;

    // ── Audio samples ─────────────────────────────────────────────────────────
    AudioRingBuffer audio_ring_;

    // ── Threads ───────────────────────────────────────────────────────────────
    std::thread demux_th_, audio_th_, video_th_;

    std::vector<Chapter>      chapters_;
    std::function<void()>     end_cb_;
    std::string               file_path_;
};
