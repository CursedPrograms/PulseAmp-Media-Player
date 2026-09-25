// ─── player.cpp ───────────────────────────────────────────────────────────────
#include "player.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <mutex>

// ─── AudioRingBuffer ──────────────────────────────────────────────────────────
int AudioRingBuffer::write(const float* src, int count) {
    int space = CAPACITY - count_.load(std::memory_order_acquire);
    int n = std::min(count, space);
    if (n <= 0) return 0;
    int w = write_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        buf_[(w + i) % CAPACITY] = src[i];
    write_.store((w + n) % CAPACITY, std::memory_order_release);
    count_.fetch_add(n, std::memory_order_release);
    return n;
}

void AudioRingBuffer::applyPendingDiscard() {
    // Consumer side only: skip everything that is buffered right now
    if (!discard_.exchange(false, std::memory_order_acq_rel)) return;
    int avail = count_.load(std::memory_order_acquire);
    int r = read_.load(std::memory_order_relaxed);
    read_.store((r + avail) % CAPACITY, std::memory_order_release);
    count_.fetch_sub(avail, std::memory_order_release);
}

int AudioRingBuffer::read(float* dst, int count) {
    applyPendingDiscard();
    int avail = count_.load(std::memory_order_acquire);
    int n = std::min(count, avail);
    if (n <= 0) {
        std::memset(dst, 0, count * sizeof(float));
        return 0;
    }
    int r = read_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dst[i] = buf_[(r + i) % CAPACITY];
    // Silence rest if short
    if (n < count) std::memset(dst + n, 0, (count - n) * sizeof(float));
    read_.store((r + n) % CAPACITY, std::memory_order_release);
    count_.fetch_sub(n, std::memory_order_release);
    return n;
}

void AudioRingBuffer::peekNext(float* dst, int count) const {
    int avail = count_.load(std::memory_order_acquire);
    int n = std::min(count, avail);
    int r = read_.load(std::memory_order_acquire);
    // Start at the read head: these samples are about to reach the speakers
    // (the tail of the buffer can be seconds ahead of what is heard)
    for (int i = 0; i < n; ++i)
        dst[i] = buf_[(r + i) % CAPACITY];
    if (n < count) std::memset(dst + n, 0, (count - n) * sizeof(float));
}

void AudioRingBuffer::reset() {
    // Only call while neither the producer nor the consumer is running
    write_.store(0); read_.store(0); count_.store(0);
    discard_.store(false);
    std::fill(buf_.begin(), buf_.end(), 0.f);
}

// ─── Player helpers ───────────────────────────────────────────────────────────
AVPacket* Player::flushPacket() {
    static char tag;
    return reinterpret_cast<AVPacket*>(&tag);
}

void Player::freePacket(AVPacket* p) {
    if (p && p != flushPacket()) av_packet_free(&p);
}

double Player::nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ─── Player ───────────────────────────────────────────────────────────────────
Player::Player() = default;

Player::~Player() { close(); }

// Open a file or URL (http options: reconnect, timeout, extra headers)
static AVFormatContext* openInput(const std::string& url, const std::string& headers) {
    AVDictionary* opts = nullptr;
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "rw_timeout", "15000000", 0); // 15 s (microseconds)
        if (!headers.empty()) av_dict_set(&opts, "headers", headers.c_str(), 0);
    }
    AVFormatContext* ctx = nullptr;
    int r = avformat_open_input(&ctx, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0) return nullptr;
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        avformat_close_input(&ctx);
        return nullptr;
    }
    return ctx;
}

PreparedMedia::~PreparedMedia() {
    if (main)  avformat_close_input(&main);
    if (audio) avformat_close_input(&audio);
}

std::unique_ptr<PreparedMedia> Player::prepare(const std::string& path, const OpenOptions& opt) {
    static std::once_flag net_once;
    std::call_once(net_once, [] { avformat_network_init(); });

    auto pm = std::make_unique<PreparedMedia>();
    pm->path = path;
    pm->opt  = opt;
    // Open the separate audio input in parallel (each open is several round trips)
    std::thread audio_th;
    if (!opt.audio_url.empty())
        audio_th = std::thread([&] { pm->audio = openInput(opt.audio_url, opt.http_headers); });
    pm->main = openInput(path, opt.http_headers);
    if (audio_th.joinable()) audio_th.join();

    if (!pm->main || (!opt.audio_url.empty() && !pm->audio)) {
        std::cerr << "[Player] Cannot open: " << path << "\n";
        return nullptr;
    }
    return pm;
}

bool Player::open(const std::string& path, const OpenOptions& opt) {
    close();
    return start(prepare(path, opt));
}

bool Player::start(std::unique_ptr<PreparedMedia> pm) {
    close();
    if (!pm || !pm->main) return false;
    const OpenOptions& opt = pm->opt;
    file_path_ = opt.source_id.empty() ? pm->path : opt.source_id;
    state_.store(PlayerState::Opening);

    // Take ownership of the opened input(s)
    fmt_ctx_ = pm->main;  pm->main = nullptr;
    inputs_[0].ctx = fmt_ctx_;
    n_inputs_  = 1;
    audio_fmt_ = fmt_ctx_;
    if (pm->audio) {
        inputs_[1].ctx = pm->audio;  pm->audio = nullptr;
        n_inputs_  = 2;
        audio_fmt_ = inputs_[1].ctx;
    }
    duration_ = 0.0;
    for (int i = 0; i < n_inputs_; ++i)
        if (inputs_[i].ctx->duration > 0)
            duration_ = std::max(duration_, (double)inputs_[i].ctx->duration / AV_TIME_BASE);

    // Find streams
    audio_stream_ = av_find_best_stream(audio_fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    video_stream_ = av_find_best_stream(fmt_ctx_,   AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    // Ignore embedded cover art (a single still "video" frame in audio files)
    if (video_stream_ >= 0 &&
        (fmt_ctx_->streams[video_stream_]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        video_stream_ = -1;

    // ── Init audio decoder ────────────────────────────────────────────────────
    if (audio_stream_ >= 0) {
        AVStream* st = audio_fmt_->streams[audio_stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) audio_ctx_ = avcodec_alloc_context3(codec);
        if (!audio_ctx_ ||
            avcodec_parameters_to_context(audio_ctx_, st->codecpar) < 0 ||
            avcodec_open2(audio_ctx_, codec, nullptr) < 0) {
            avcodec_free_context(&audio_ctx_);
            audio_stream_ = -1;
        } else {
            // Setup resampler → float32 stereo
            swr_ctx_ = swr_alloc();
            av_opt_set_chlayout  (swr_ctx_, "in_chlayout",   &audio_ctx_->ch_layout, 0);
            av_opt_set_int       (swr_ctx_, "in_sample_rate",  audio_ctx_->sample_rate, 0);
            av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt",   audio_ctx_->sample_fmt, 0);
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_opt_set_chlayout  (swr_ctx_, "out_chlayout",  &stereo, 0);
            av_opt_set_int       (swr_ctx_, "out_sample_rate", out_sample_rate_, 0);
            av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt",  AV_SAMPLE_FMT_FLT, 0);
            if (swr_init(swr_ctx_) < 0) {
                swr_free(&swr_ctx_);
                avcodec_free_context(&audio_ctx_);
                audio_stream_ = -1;
            }
        }
    }

    // ── Init video decoder ────────────────────────────────────────────────────
    if (video_stream_ >= 0) {
        AVStream* st = fmt_ctx_->streams[video_stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) video_ctx_ = avcodec_alloc_context3(codec);
        if (video_ctx_) video_ctx_->thread_count = 4;
        if (!video_ctx_ ||
            avcodec_parameters_to_context(video_ctx_, st->codecpar) < 0 ||
            avcodec_open2(video_ctx_, codec, nullptr) < 0) {
            avcodec_free_context(&video_ctx_);
            video_stream_ = -1;
        } else {
            video_width_  = video_ctx_->width;
            video_height_ = video_ctx_->height;
            // sws_ctx_ is created per frame format in videoDecodeLoop()
        }
    }

    audio_tb_ = audio_stream_ >= 0 ? av_q2d(audio_fmt_->streams[audio_stream_]->time_base) : 0.0;
    video_tb_ = video_stream_ >= 0 ? av_q2d(fmt_ctx_->streams[video_stream_]->time_base) : 0.0;
    inputs_[0].feeds_video = video_stream_ >= 0;
    inputs_[0].feeds_audio = audio_stream_ >= 0 && audio_fmt_ == fmt_ctx_;
    if (n_inputs_ == 2) inputs_[1].feeds_audio = audio_stream_ >= 0;

    parseChapters();
    audio_ring_.reset();

    // Reset clock and end-of-file state
    audio_clock_.store(0.0);
    wall_base_.store(0.0);
    wall_start_.store(nowSeconds());
    audio_drained_.store(false);
    video_drained_.store(false);
    end_reported_.store(false);

    running_.store(true);
    state_.store(PlayerState::Playing);

    // Start in sync with the seek serial: only seeks requested from now on apply
    seek_target_.store(0.0);
    const int serial = seek_serial_.load();
    for (int i = 0; i < n_inputs_; ++i) {
        inputs_[i].seen_seek = serial;
        inputs_[i].eof = false;
        inputs_[i].th = std::thread(&Player::demuxLoop, this, i);
    }
    audio_th_  = std::thread(&Player::audioDecodeLoop,  this);
    video_th_  = std::thread(&Player::videoDecodeLoop,  this);
    return true;
}

void Player::close() {
    running_.store(false);
    // Wake blocked threads
    audio_pkt_cv_.notify_all();
    video_pkt_cv_.notify_all();
    video_frame_cv_.notify_all();

    for (auto& in : inputs_)
        if (in.th.joinable()) in.th.join();
    if (audio_th_.joinable()) audio_th_.join();
    if (video_th_.joinable()) video_th_.join();

    // Drain queues
    auto drainQ = [](auto& q, auto& mu) {
        std::lock_guard lk(mu);
        while (!q.empty()) { freePacket(q.front()); q.pop(); }
    };
    drainQ(audio_pkts_, audio_pkt_mu_);
    drainQ(video_pkts_, video_pkt_mu_);
    { std::lock_guard lk(video_frame_mu_);
      while (!video_frames_.empty()) video_frames_.pop(); }

    if (swr_ctx_)   { swr_free(&swr_ctx_); }
    if (sws_ctx_)   { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
    if (audio_ctx_) { avcodec_free_context(&audio_ctx_); }
    if (video_ctx_) { avcodec_free_context(&video_ctx_); }
    for (auto& in : inputs_) {
        if (in.ctx) avformat_close_input(&in.ctx);
        in.feeds_audio = in.feeds_video = false;
        in.eof = false;
    }
    n_inputs_ = 0;
    fmt_ctx_ = audio_fmt_ = nullptr;

    audio_stream_ = video_stream_ = -1;
    duration_ = 0.0;
    audio_clock_.store(0.0); wall_base_.store(0.0);
    state_.store(PlayerState::Stopped);
    chapters_.clear();
}

void Player::play() {
    if (state_.load() == PlayerState::Paused) {
        wall_start_.store(nowSeconds());
        state_.store(PlayerState::Playing);
    }
}

void Player::pause() {
    if (state_.load() == PlayerState::Playing) {
        wall_base_.store(getCurrentTime());
        state_.store(PlayerState::Paused);
    }
}

void Player::stop() {
    close();
}

void Player::seek(double seconds) {
    seconds = std::max(0.0, seconds);
    if (duration_ > 0.0) seconds = std::min(seconds, duration_);
    seek_target_.store(seconds);
    seek_serial_.fetch_add(1);   // demux + decode threads pick this up
}

double Player::getCurrentTime() const {
    double t;
    if (audio_stream_ >= 0) {
        // Audible time = newest decoded audio minus what is still waiting in the ring
        double buffered = audio_ring_.discardPending()
            ? 0.0
            : audio_ring_.available() / (2.0 * out_sample_rate_);
        t = audio_clock_.load() - buffered;
    } else {
        t = wall_base_.load();
        if (state_.load() == PlayerState::Playing)
            t += nowSeconds() - wall_start_.load();
    }
    if (duration_ > 0.0) t = std::min(t, duration_);
    return std::max(0.0, t);
}

bool Player::pollEnded() {
    if (end_reported_.load() || n_inputs_ == 0) return false;

    // With audio, the audio input decides; otherwise every input must be done
    bool eof = true;
    for (int i = 0; i < n_inputs_; ++i)
        if (audio_stream_ < 0 || inputs_[i].feeds_audio)
            eof = eof && inputs_[i].eof.load();
    if (!eof) return false;

    bool audio_done = audio_stream_ < 0 ||
        (audio_drained_.load() && audio_ring_.available() == 0);

    // With audio, the audio clock decides when we're done
    bool video_done = audio_stream_ >= 0 || video_stream_ < 0;
    if (!video_done && video_drained_.load()) {
        std::lock_guard lk(video_frame_mu_);
        video_done = video_frames_.empty();
    }

    if (audio_done && video_done) {
        end_reported_.store(true);
        return true;
    }
    return false;
}

// ─── Demux loop ───────────────────────────────────────────────────────────────
void Player::demuxLoop(int idx) {
    Input& in = inputs_[idx];
    AVPacket* pkt = av_packet_alloc();
    bool eof = false;

    while (running_.load()) {
        // Handle a new seek request
        const int req = seek_serial_.load();
        if (req != in.seen_seek) {
            in.seen_seek = req;
            double t = seek_target_.load();
            int64_t ts = (int64_t)(t * AV_TIME_BASE);
            av_seek_frame(in.ctx, -1, ts, AVSEEK_FLAG_BACKWARD);

            // Drop queued packets and tell each decoder thread to flush itself
            // (decoders are only ever touched by their own thread)
            if (in.feeds_audio) {
                { std::lock_guard lk(audio_pkt_mu_);
                  while (!audio_pkts_.empty()) { freePacket(audio_pkts_.front()); audio_pkts_.pop(); }
                  audio_pkts_.push(flushPacket()); audio_pkt_cv_.notify_one(); }
                audio_drained_.store(false);
            }
            if (in.feeds_video) {
                { std::lock_guard lk(video_pkt_mu_);
                  while (!video_pkts_.empty()) { freePacket(video_pkts_.front()); video_pkts_.pop(); }
                  video_pkts_.push(flushPacket()); video_pkt_cv_.notify_one(); }
                { std::lock_guard lk(video_frame_mu_);
                  while (!video_frames_.empty()) video_frames_.pop();
                  video_frame_cv_.notify_all(); }
                video_drained_.store(false);
            }

            audio_clock_.store(t);
            wall_base_.store(t);
            wall_start_.store(nowSeconds());
            eof = false;
            in.eof.store(false);
            end_reported_.store(false);
        }

        // At end of file: wait for a seek or close
        if (eof) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Back-pressure: don't over-buffer (and never drop packets)
        bool full = false;
        if (in.feeds_audio) { std::lock_guard lk(audio_pkt_mu_); full = (int)audio_pkts_.size() >= MAX_AUDIO_PKTS; }
        if (!full && in.feeds_video) { std::lock_guard lk(video_pkt_mu_); full = (int)video_pkts_.size() >= MAX_VIDEO_PKTS; }
        if (full) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        int ret = av_read_frame(in.ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF || (in.ctx->pb && avio_feof(in.ctx->pb))) {
                eof = true;
                // nullptr = drain the decoder
                if (in.feeds_audio) {
                    std::lock_guard lk(audio_pkt_mu_);
                    audio_pkts_.push(nullptr);
                    audio_pkt_cv_.notify_one();
                }
                if (in.feeds_video) {
                    std::lock_guard lk(video_pkt_mu_);
                    video_pkts_.push(nullptr);
                    video_pkt_cv_.notify_one();
                }
                in.eof.store(true);
            } else {
                // Transient read error: back off instead of spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (in.feeds_audio && pkt->stream_index == audio_stream_) {
            AVPacket* p = av_packet_alloc();
            av_packet_move_ref(p, pkt);
            std::lock_guard lk(audio_pkt_mu_);
            audio_pkts_.push(p);
            audio_pkt_cv_.notify_one();
        } else if (in.feeds_video && pkt->stream_index == video_stream_) {
            AVPacket* p = av_packet_alloc();
            av_packet_move_ref(p, pkt);
            std::lock_guard lk(video_pkt_mu_);
            video_pkts_.push(p);
            video_pkt_cv_.notify_one();
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

// ─── Audio decode loop ────────────────────────────────────────────────────────
void Player::audioDecodeLoop() {
    AVFrame* frame = av_frame_alloc();
    std::vector<float> resample_buf;
    const double tb = audio_tb_;
    double skip_until = -1.0; // after a seek: drop audio before the target

    while (running_.load()) {
        AVPacket* pkt = nullptr;
        {
            std::unique_lock lk(audio_pkt_mu_);
            audio_pkt_cv_.wait_for(lk, std::chrono::milliseconds(20),
                [&]{ return !audio_pkts_.empty() || !running_.load(); });
            if (!running_.load()) break;
            if (audio_pkts_.empty()) continue;
            pkt = audio_pkts_.front();
            audio_pkts_.pop();
        }

        if (pkt == flushPacket()) {
            // Seek: reset decoder + resampler, drop what is buffered for playback
            avcodec_flush_buffers(audio_ctx_);
            swr_init(swr_ctx_);
            audio_ring_.requestDiscard();
            // Wait for the audio callback to drop the stale samples before writing
            // new ones, otherwise fresh post-seek audio would be dropped too.
            for (int i = 0; i < 125 && running_.load() && audio_ring_.discardPending(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            // No audio device consuming the ring: nothing else can touch it
            if (audio_ring_.discardPending()) audio_ring_.applyPendingDiscard();
            skip_until = seek_target_.load();
            audio_clock_.store(skip_until);
            continue;
        }

        const int  serial = seek_serial_.load();
        const bool drain  = (pkt == nullptr);
        avcodec_send_packet(audio_ctx_, pkt);
        freePacket(pkt);

        while (true) {
            int ret = avcodec_receive_frame(audio_ctx_, frame);
            if (ret < 0) break; // EAGAIN, EOF or error

            double pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                ? frame->best_effort_timestamp * tb
                : audio_clock_.load();

            // Seeking lands on the keyframe before the target; skip up to the target
            if (pts + (double)frame->nb_samples / audio_ctx_->sample_rate <= skip_until) {
                av_frame_unref(frame);
                continue;
            }

            // Resample to float32 stereo
            int out_samples = (int)av_rescale_rnd(
                swr_get_delay(swr_ctx_, audio_ctx_->sample_rate) + frame->nb_samples,
                out_sample_rate_, audio_ctx_->sample_rate, AV_ROUND_UP);
            resample_buf.resize(out_samples * 2);
            uint8_t* out_ptr = (uint8_t*)resample_buf.data();
            int converted = swr_convert(swr_ctx_, &out_ptr, out_samples,
                (const uint8_t**)frame->data, frame->nb_samples);
            av_frame_unref(frame);
            if (converted <= 0) continue;

            // Write to ring (volume, mute and effects are applied at output time,
            // so they react instantly instead of after the buffer drains).
            // Give up if a seek happens meanwhile - this audio is stale.
            const int total = converted * 2;
            int done = 0;
            while (done < total && running_.load() && serial == seek_serial_.load()) {
                done += audio_ring_.write(resample_buf.data() + done, total - done);
                if (done < total)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (serial == seek_serial_.load())
                audio_clock_.store(pts + (double)converted / out_sample_rate_);
        }

        if (drain && serial == seek_serial_.load()) audio_drained_.store(true);
    }
    av_frame_free(&frame);
}

// ─── Video decode loop ────────────────────────────────────────────────────────
void Player::videoDecodeLoop() {
    AVFrame* frame = av_frame_alloc();
    const double tb = video_tb_;
    double last_pts = 0.0;

    while (running_.load()) {
        // Throttle: wait until there is room in the frame queue
        {
            std::unique_lock lk(video_frame_mu_);
            video_frame_cv_.wait_for(lk, std::chrono::milliseconds(10),
                [&]{ return (int)video_frames_.size() < MAX_VIDEO_FRAMES || !running_.load(); });
            if ((int)video_frames_.size() >= MAX_VIDEO_FRAMES) continue;
        }
        if (!running_.load()) break;

        AVPacket* pkt = nullptr;
        {
            std::unique_lock lk(video_pkt_mu_);
            video_pkt_cv_.wait_for(lk, std::chrono::milliseconds(20),
                [&]{ return !video_pkts_.empty() || !running_.load(); });
            if (!running_.load()) break;
            if (video_pkts_.empty()) continue;
            pkt = video_pkts_.front();
            video_pkts_.pop();
        }

        if (pkt == flushPacket()) {
            avcodec_flush_buffers(video_ctx_);
            std::lock_guard lk(video_frame_mu_);
            while (!video_frames_.empty()) video_frames_.pop();
            continue;
        }

        const int  serial = seek_serial_.load();
        const bool drain  = (pkt == nullptr);
        avcodec_send_packet(video_ctx_, pkt);
        freePacket(pkt);

        while (true) {
            int ret = avcodec_receive_frame(video_ctx_, frame);
            if (ret < 0) break; // EAGAIN, EOF or error

            // (Re)create the scaler for this frame's size/format
            sws_ctx_ = sws_getCachedContext(sws_ctx_,
                frame->width, frame->height, (AVPixelFormat)frame->format,
                frame->width, frame->height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws_ctx_) { av_frame_unref(frame); continue; }

            // Convert to RGBA
            auto vf = std::make_shared<VideoFrame>();
            vf->width  = frame->width;
            vf->height = frame->height;
            vf->rgba.resize((size_t)frame->width * frame->height * 4);
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                last_pts = frame->best_effort_timestamp * tb;
            vf->pts = last_pts;

            uint8_t* dst[4] = { vf->rgba.data(), nullptr, nullptr, nullptr };
            int      ls[4]  = { frame->width * 4, 0, 0, 0 };
            sws_scale(sws_ctx_,
                (const uint8_t* const*)frame->data, frame->linesize,
                0, frame->height, dst, ls);
            av_frame_unref(frame);

            if (serial != seek_serial_.load()) continue; // stale after a seek
            std::lock_guard lk(video_frame_mu_);
            video_frames_.push(vf);
            video_frame_cv_.notify_one();
        }

        if (drain && serial == seek_serial_.load()) video_drained_.store(true);
    }
    av_frame_free(&frame);
}

// ─── Chapters ─────────────────────────────────────────────────────────────────
void Player::parseChapters() {
    chapters_.clear();
    if (!fmt_ctx_ || fmt_ctx_->nb_chapters == 0) return;
    for (unsigned i = 0; i < fmt_ctx_->nb_chapters; ++i) {
        AVChapter* ch = fmt_ctx_->chapters[i];
        Chapter c;
        c.start = ch->start * av_q2d(ch->time_base);
        c.end   = ch->end   * av_q2d(ch->time_base);
        AVDictionaryEntry* t = av_dict_get(ch->metadata, "title", nullptr, 0);
        c.title = t ? t->value : ("Chapter " + std::to_string(i + 1));
        chapters_.push_back(c);
    }
}

std::shared_ptr<VideoFrame> Player::pollVideoFrame() {
    // Return the newest frame that is due (dropping any we're late for)
    const double now = getCurrentTime() + 0.02;
    std::lock_guard lk(video_frame_mu_);
    std::shared_ptr<VideoFrame> due;
    while (!video_frames_.empty() && video_frames_.front()->pts <= now) {
        due = video_frames_.front();
        video_frames_.pop();
    }
    if (due) video_frame_cv_.notify_one();
    return due;
}
