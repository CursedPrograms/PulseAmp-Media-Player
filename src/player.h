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
    // Visualizer tap: copy of the next samples to be played (what is heard
    // right now), without consuming them. Pads with silence if short.
    void peekNext(float* dst, int count) const;

    // Ask the consumer to drop everything currently buffered (used after a seek).
    // Safe to call from the producer thread; the drop happens in the next read().
    void requestDiscard() { discard_.store(true, std::memory_order_release); }
    bool discardPending() const { return discard_.load(std::memory_order_acquire); }
    // Consumer side: perform a requested discard now (read() does this itself)
    void applyPendingDiscard();

    void reset();
    int available() const { return count_.load(std::memory_order_acquire); }

private:
    std::vector<float>  buf_;
    std::atomic<int>    write_{ 0 };
    std::atomic<int>    read_ { 0 };
    std::atomic<int>    count_{ 0 };
    std::atomic<bool>   discard_{ false };
};

// ─── Open options ─────────────────────────────────────────────────────────────
struct OpenOptions {
    std::string audio_url;     // separate audio input (e.g. YouTube video + audio streams)
    std::string http_headers;  // "Name: value\r\n..." sent with http(s) requests
    std::string source_id;     // what getFilePath() reports (page URL for online media)
};

// Inputs opened ahead of time (network I/O done). Created by Player::prepare()
// on any thread, then handed to Player::start() on the main thread.
struct PreparedMedia {
    std::string      path;
    OpenOptions      opt;
    AVFormatContext* main  = nullptr;
    AVFormatContext* audio = nullptr;   // only with opt.audio_url
    PreparedMedia() = default;
    PreparedMedia(const PreparedMedia&) = delete;
    PreparedMedia& operator=(const PreparedMedia&) = delete;
    ~PreparedMedia();
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
    bool        open(const std::string& path, const OpenOptions& opt = {});
    // open() in two steps, so slow (network) opens can run off the UI thread:
    static std::unique_ptr<PreparedMedia> prepare(const std::string& path,
                                                  const OpenOptions& opt = {});
    bool        start(std::unique_ptr<PreparedMedia> media);
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
    double              getCurrentTime() const; // playback clock (what is being heard/seen)
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

    // ── End of media ──────────────────────────────────────────────────────────
    // Returns true once when playback has reached the end of the file.
    // Call from the main thread (the UI then advances the playlist there,
    // never from inside a decode thread).
    bool pollEnded();

private:
    void demuxLoop(int input);
    void audioDecodeLoop();
    void videoDecodeLoop();
    void parseChapters();

    // Sentinel pushed into packet queues after a seek: "flush your decoder".
    // (nullptr in a queue means "end of file, drain the decoder".)
    static AVPacket* flushPacket();
    static void      freePacket(AVPacket* p);

    // Wall clock used when the file has no audio stream
    static double    nowSeconds();

    // ── FFmpeg state ──────────────────────────────────────────────────────────
    // One or two inputs, each read by its own demux thread
    struct Input {
        AVFormatContext*  ctx = nullptr;
        bool              feeds_audio = false, feeds_video = false;
        int               seen_seek = 0;       // last seek request handled
        std::atomic<bool> eof{ false };        // this input reached end of file
        std::thread       th;
    };
    Input             inputs_[2];
    int               n_inputs_  = 0;
    AVFormatContext*  fmt_ctx_   = nullptr;   // primary input (video, chapters)
    AVFormatContext*  audio_fmt_ = nullptr;   // input holding the audio stream
    double            audio_tb_  = 0.0;       // stream time bases in seconds
    double            video_tb_  = 0.0;
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
    std::atomic<float>       volume_{ 1.0f };
    std::atomic<bool>        muted_{ false };
    std::atomic<double>      seek_target_{ 0.0 };
    std::atomic<int>         seek_serial_{ 0 };   // bumped on every seek request
    std::atomic<bool>        running_{ false };

    // ── Clock ─────────────────────────────────────────────────────────────────
    // With audio: pts at the end of the last sample written to the ring buffer;
    // the audible time is that minus what is still buffered.
    std::atomic<double>      audio_clock_{ 0.0 };
    // Without audio: media time at wall_start_, advanced by wall time while playing.
    std::atomic<double>      wall_base_{ 0.0 };
    std::atomic<double>      wall_start_{ 0.0 };

    // ── End-of-file tracking ──────────────────────────────────────────────────
    std::atomic<bool>        audio_drained_{ false };
    std::atomic<bool>        video_drained_{ false };
    std::atomic<bool>        end_reported_{ false };

    // ── Packet queues (demux → decode threads) ────────────────────────────────
    static constexpr int MAX_AUDIO_PKTS = 128;
    static constexpr int MAX_VIDEO_PKTS = 64;

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
    std::thread audio_th_, video_th_;

    std::vector<Chapter>      chapters_;
    std::string               file_path_;
};
