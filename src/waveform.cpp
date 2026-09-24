// ─── waveform.cpp ─────────────────────────────────────────────────────────────
#include "waveform.h"
#include <cmath>
#include <algorithm>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

WaveformGenerator::WaveformGenerator() = default;

WaveformGenerator::~WaveformGenerator() { cancel(); }

void WaveformGenerator::cancel() {
    cancel_.store(true);
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
}

void WaveformGenerator::generate(const std::string& path, int num_buckets,
                                  std::function<void(std::vector<float>)> cb) {
    cancel();
    running_.store(true);
    cancel_.store(false);
    thread_ = std::thread(&WaveformGenerator::generateThread, this, path, num_buckets, std::move(cb));
}

void WaveformGenerator::generateThread(std::string path, int num_buckets,
                                        std::function<void(std::vector<float>)> callback) {
    std::vector<float> peaks(num_buckets, 0.f);

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        running_.store(false); callback(peaks); return;
    }
    avformat_find_stream_info(fmt, nullptr);

    int audio_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_idx < 0) {
        avformat_close_input(&fmt);
        running_.store(false); callback(peaks); return;
    }

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[audio_idx]->codecpar->codec_id);
    AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx ||
        avcodec_parameters_to_context(ctx, fmt->streams[audio_idx]->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0) {
        // No usable decoder for this audio stream
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        running_.store(false); callback(peaks); return;
    }

    SwrContext* swr = swr_alloc();
    AVChannelLayout mono_layout = AV_CHANNEL_LAYOUT_MONO;
    av_opt_set_chlayout  (swr, "in_chlayout",  &ctx->ch_layout, 0);
    av_opt_set_int       (swr, "in_sample_rate", ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  ctx->sample_fmt, 0);
    av_opt_set_chlayout  (swr, "out_chlayout",  &mono_layout, 0);
    av_opt_set_int       (swr, "out_sample_rate", 22050, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt",  AV_SAMPLE_FMT_FLT, 0);
    swr_init(swr);

    double duration = fmt->duration > 0 ? (double)fmt->duration / AV_TIME_BASE : 0.0;
    if (duration <= 0.0) { /* estimate */ duration = 180.0; }

    std::vector<double> bucket_sum(num_buckets, 0.0);
    std::vector<int>    bucket_cnt(num_buckets, 0);

    AVPacket* pkt  = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    std::vector<float> tmp(4096);

    while (!cancel_.load() && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != audio_idx) { av_packet_unref(pkt); continue; }
        avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(ctx, frame) == 0) {
            double pts = frame->best_effort_timestamp
                * av_q2d(fmt->streams[audio_idx]->time_base);
            int bucket = (int)(pts / duration * num_buckets);
            bucket = std::clamp(bucket, 0, num_buckets - 1);

            int out_n = (int)av_rescale_rnd(
                swr_get_delay(swr, ctx->sample_rate) + frame->nb_samples,
                22050, ctx->sample_rate, AV_ROUND_UP);
            tmp.resize(out_n);
            uint8_t* outp = (uint8_t*)tmp.data();
            int got = swr_convert(swr, &outp, out_n,
                (const uint8_t**)frame->data, frame->nb_samples);
            // RMS
            double rms = 0.0;
            for (int i = 0; i < got; ++i) rms += (double)tmp[i] * tmp[i];
            rms = std::sqrt(rms / std::max(1, got));
            bucket_sum[bucket] += rms;
            bucket_cnt[bucket]++;
            av_frame_unref(frame);
        }
    }

    // Normalise
    float maxVal = 1e-6f;
    for (int i = 0; i < num_buckets; ++i) {
        peaks[i] = bucket_cnt[i] > 0
            ? (float)(bucket_sum[i] / bucket_cnt[i])
            : 0.f;
        maxVal = std::max(maxVal, peaks[i]);
    }
    for (auto& v : peaks) v /= maxVal;

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    running_.store(false);
    if (!cancel_.load()) callback(peaks);
}
