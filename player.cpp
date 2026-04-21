// ─── player.cpp ───────────────────────────────────────────────────────────────
#include "player.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>

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

int AudioRingBuffer::read(float* dst, int count) {
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

void AudioRingBuffer::peekLatest(float* dst, int count) const {
    int avail = count_.load(std::memory_order_acquire);
    int n = std::min(count, avail);
    int r = read_.load(std::memory_order_relaxed);
    // Read the most recent 'n' samples (tail of what's in buffer)
    int start = (r + avail - n + CAPACITY) % CAPACITY;
    for (int i = 0; i < n; ++i)
        dst[i] = buf_[(start + i) % CAPACITY];
    if (n < count) std::memset(dst + n, 0, (count - n) * sizeof(float));
}

void AudioRingBuffer::reset() {
    write_.store(0); read_.store(0); count_.store(0);
    std::fill(buf_.begin(), buf_.end(), 0.f);
}

// ─── Player ───────────────────────────────────────────────────────────────────
Player::Player() = default;

Player::~Player() { close(); }

bool Player::open(const std::string& path) {
    close();
    file_path_ = path;
    state_.store(PlayerState::Opening);

    // Open input
    fmt_ctx_ = avformat_alloc_context();
    if (avformat_open_input(&fmt_ctx_, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[Player] Cannot open: " << path << "\n";
        state_.store(PlayerState::Stopped);
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
        state_.store(PlayerState::Stopped);
        return false;
    }
    duration_ = fmt_ctx_->duration > 0
        ? (double)fmt_ctx_->duration / AV_TIME_BASE
        : 0.0;

    // Find streams
    audio_stream_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    video_stream_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    // ── Init audio decoder ────────────────────────────────────────────────────
    if (audio_stream_ >= 0) {
        AVStream* st = fmt_ctx_->streams[audio_stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) { audio_stream_ = -1; goto skip_audio; }
        audio_ctx_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(audio_ctx_, st->codecpar);
        if (avcodec_open2(audio_ctx_, codec, nullptr) < 0) {
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
            swr_init(swr_ctx_);
        }
    }
    skip_audio:;

    // ── Init video decoder ────────────────────────────────────────────────────
    if (video_stream_ >= 0) {
        AVStream* st = fmt_ctx_->streams[video_stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) { video_stream_ = -1; goto skip_video; }
        video_ctx_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(video_ctx_, st->codecpar);
        video_ctx_->thread_count = 4;
        if (avcodec_open2(video_ctx_, codec, nullptr) < 0) {
            avcodec_free_context(&video_ctx_);
            video_stream_ = -1;
        } else {
            video_width_  = video_ctx_->width;
            video_height_ = video_ctx_->height;
            sws_ctx_ = sws_getContext(
                video_width_, video_height_, video_ctx_->pix_fmt,
                video_width_, video_height_, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
    }
    skip_video:;

    parseChapters();
    audio_ring_.reset();
    running_.store(true);

    demux_th_  = std::thread(&Player::demuxLoop,        this);
    audio_th_  = std::thread(&Player::audioDecodeLoop,  this);
    video_th_  = std::thread(&Player::videoDecodeLoop,  this);

    state_.store(PlayerState::Playing);
    return true;
}

void Player::close() {
    running_.store(false);
    // Wake blocked threads
    audio_pkt_cv_.notify_all();
    video_pkt_cv_.notify_all();
    video_frame_cv_.notify_all();

    if (demux_th_.joinable()) demux_th_.join();
    if (audio_th_.joinable()) audio_th_.join();
    if (video_th_.joinable()) video_th_.join();

    // Drain queues
    auto drainQ = [](auto& q, auto& mu) {
        std::lock_guard lk(mu);
        while (!q.empty()) { av_packet_free(&q.front()); q.pop(); }
    };
    drainQ(audio_pkts_, audio_pkt_mu_);
    drainQ(video_pkts_, video_pkt_mu_);
    { std::lock_guard lk(video_frame_mu_);
      while (!video_frames_.empty()) video_frames_.pop(); }

    if (swr_ctx_)   { swr_free(&swr_ctx_); }
    if (sws_ctx_)   { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
    if (audio_ctx_) { avcodec_free_context(&audio_ctx_); }
    if (video_ctx_) { avcodec_free_context(&video_ctx_); }
    if (fmt_ctx_)   { avformat_close_input(&fmt_ctx_); }

    audio_stream_ = video_stream_ = -1;
    duration_ = 0.0; current_time_.store(0.0);
    state_.store(PlayerState::Stopped);
    chapters_.clear();
}

void Player::play() {
    if (state_.load() == PlayerState::Paused)
        state_.store(PlayerState::Playing);
}

void Player::pause() {
    if (state_.load() == PlayerState::Playing)
        state_.store(PlayerState::Paused);
}

void Player::stop() {
    state_.store(PlayerState::Stopped);
    close();
}

void Player::seek(double seconds) {
    seek_target_.store(std::max(0.0, std::min(seconds, duration_)));
    seek_requested_.store(true);
}

// ─── Demux loop ───────────────────────────────────────────────────────────────
void Player::demuxLoop() {
    AVPacket* pkt = av_packet_alloc();
    bool eof = false;

    while (running_.load()) {
        // Handle seek
        if (seek_requested_.exchange(false)) {
            double t = seek_target_.load();
            int64_t ts = (int64_t)(t * AV_TIME_BASE);
            av_seek_frame(fmt_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (audio_ctx_) avcodec_flush_buffers(audio_ctx_);
            if (video_ctx_) avcodec_flush_buffers(video_ctx_);
            audio_ring_.reset();
            { std::lock_guard lk(audio_pkt_mu_);
              while (!audio_pkts_.empty()) { av_packet_free(&audio_pkts_.front()); audio_pkts_.pop(); } }
            { std::lock_guard lk(video_pkt_mu_);
              while (!video_pkts_.empty()) { av_packet_free(&video_pkts_.front()); video_pkts_.pop(); } }
            current_time_.store(t);
            eof = false;
        }

        // Pause: just wait
        if (state_.load() == PlayerState::Paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Back-pressure: don't over-buffer
        {
            std::unique_lock lk(audio_pkt_mu_);
            if ((int)audio_pkts_.size() >= MAX_AUDIO_PKTS) {
                lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
        }

        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(fmt_ctx_->pb)) {
                if (!eof) {
                    eof = true;
                    // Flush decoders
                    if (audio_stream_ >= 0) {
                        std::lock_guard lk(audio_pkt_mu_);
                        audio_pkts_.push(nullptr); // null = flush
                    }
                    if (video_stream_ >= 0) {
                        std::lock_guard lk(video_pkt_mu_);
                        video_pkts_.push(nullptr);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }

        if (pkt->stream_index == audio_stream_ && audio_ctx_) {
            AVPacket* p = av_packet_clone(pkt);
            std::lock_guard lk(audio_pkt_mu_);
            audio_pkts_.push(p);
            audio_pkt_cv_.notify_one();
        } else if (pkt->stream_index == video_stream_ && video_ctx_) {
            std::unique_lock lk(video_pkt_mu_);
            if ((int)video_pkts_.size() < MAX_VIDEO_PKTS) {
                video_pkts_.push(av_packet_clone(pkt));
                video_pkt_cv_.notify_one();
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

// ─── Audio decode loop ────────────────────────────────────────────────────────
void Player::audioDecodeLoop() {
    AVFrame* frame = av_frame_alloc();
    std::vector<float> resample_buf;

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

        bool flush = (pkt == nullptr);
        avcodec_send_packet(audio_ctx_, pkt);
        if (pkt) av_packet_free(&pkt);

        while (true) {
            int ret = avcodec_receive_frame(audio_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // Update current time from audio PTS
            double pts = frame->best_effort_timestamp
                * av_q2d(fmt_ctx_->streams[audio_stream_]->time_base);
            if (pts > 0) current_time_.store(pts);

            // Resample to float32 stereo
            int out_samples = (int)av_rescale_rnd(
                swr_get_delay(swr_ctx_, audio_ctx_->sample_rate) + frame->nb_samples,
                out_sample_rate_, audio_ctx_->sample_rate, AV_ROUND_UP);
            resample_buf.resize(out_samples * 2);
            uint8_t* out_ptr = (uint8_t*)resample_buf.data();
            int converted = swr_convert(swr_ctx_, &out_ptr, out_samples,
                (const uint8_t**)frame->data, frame->nb_samples);
            if (converted > 0) {
                // Apply volume
                float vol = muted_.load() ? 0.f : volume_.load();
                for (int i = 0; i < converted * 2; ++i)
                    resample_buf[i] *= vol;
                // Write to ring
                while (running_.load()) {
                    int written = audio_ring_.write(resample_buf.data(), converted * 2);
                    if (written == converted * 2) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            av_frame_unref(frame);
        }

        if (flush && end_cb_) end_cb_();
    }
    av_frame_free(&frame);
}

// ─── Video decode loop ────────────────────────────────────────────────────────
void Player::videoDecodeLoop() {
    AVFrame* frame     = av_frame_alloc();
    AVFrame* frame_rgb = av_frame_alloc();

    while (running_.load()) {
        // Throttle: don't over-fill video frame queue
        {
            std::unique_lock lk(video_frame_mu_);
            video_frame_cv_.wait_for(lk, std::chrono::milliseconds(10),
                [&]{ return (int)video_frames_.size() < MAX_VIDEO_FRAMES || !running_.load(); });
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

        avcodec_send_packet(video_ctx_, pkt);
        if (pkt) av_packet_free(&pkt);

        while (true) {
            int ret = avcodec_receive_frame(video_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // Convert to RGBA
            auto vf = std::make_shared<VideoFrame>();
            vf->width  = video_width_;
            vf->height = video_height_;
            vf->rgba.resize(video_width_ * video_height_ * 4);
            vf->pts = frame->best_effort_timestamp
                * av_q2d(fmt_ctx_->streams[video_stream_]->time_base);

            uint8_t* dst[4] = { vf->rgba.data(), nullptr, nullptr, nullptr };
            int      ls[4]  = { video_width_ * 4, 0, 0, 0 };
            sws_scale(sws_ctx_,
                (const uint8_t* const*)frame->data, frame->linesize,
                0, video_height_, dst, ls);

            {
                std::lock_guard lk(video_frame_mu_);
                video_frames_.push(vf);
                video_frame_cv_.notify_one();
            }
            av_frame_unref(frame);
        }
    }
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
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
    // Only show frame if PTS is close to current time (±50ms)
    std::lock_guard lk(video_frame_mu_);
    if (video_frames_.empty()) return nullptr;
    auto& front = video_frames_.front();
    if (front->pts <= current_time_.load() + 0.05) {
        auto f = front;
        video_frames_.pop();
        video_frame_cv_.notify_one();
        return f;
    }
    return nullptr;
}
