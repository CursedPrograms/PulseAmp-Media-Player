// ─── converter.cpp ────────────────────────────────────────────────────────────
#include "converter.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace fs = std::filesystem;

Converter::Converter() = default;

void Converter::cancel() {
    cancel_.store(true);
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    running_.store(false);
}

void Converter::start(const ConvertJob& job,
                      std::function<void(ConvertProgress)> progress_cb) {
    cancel();
    running_.store(true);
    cancel_.store(false);
    thread_ = std::thread(&Converter::convertThread, this, job, std::move(progress_cb));
}

std::vector<std::string> Converter::availableFormats() {
    return { "mp3", "flac", "wav", "ogg", "aac", "opus",
             "mp4", "mkv", "avi", "webm", "mov" };
}

// ─── Core conversion ─────────────────────────────────────────────────────────
void Converter::convertThread(ConvertJob job,
                               std::function<void(ConvertProgress)> cb) {
    ConvertProgress prog;
    auto fail = [&](const std::string& msg) {
        prog.error = true; prog.error_msg = msg; prog.done = true;
        cb(prog); running_.store(false);
    };

    // ── Open input ────────────────────────────────────────────────────────────
    AVFormatContext* in_fmt = nullptr;
    if (avformat_open_input(&in_fmt, job.input_path.c_str(), nullptr, nullptr) < 0)
        return fail("Cannot open input: " + job.input_path);
    avformat_find_stream_info(in_fmt, nullptr);

    prog.duration = in_fmt->duration > 0
        ? (double)in_fmt->duration / AV_TIME_BASE : 0.0;

    // ── Open output ───────────────────────────────────────────────────────────
    AVFormatContext* out_fmt = nullptr;
    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr,
                                       job.output_path.c_str()) < 0)
        return fail("Cannot create output context");

    // ── Set up stream mappings ────────────────────────────────────────────────
    struct StreamMap {
        int        in_idx;
        AVStream*  out_st;
        AVCodecContext* enc_ctx;
        AVCodecContext* dec_ctx;
        SwrContext* swr;
        SwsContext* sws;
        bool        is_audio;
    };
    std::vector<StreamMap> maps;

    for (unsigned i = 0; i < in_fmt->nb_streams; ++i) {
        AVStream* in_st = in_fmt->streams[i];
        AVMediaType type = in_st->codecpar->codec_type;

        if (type == AVMEDIA_TYPE_AUDIO && job.strip_audio) continue;
        if (type == AVMEDIA_TYPE_VIDEO && job.strip_video) continue;
        if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_VIDEO) continue;

        StreamMap sm{};
        sm.in_idx   = (int)i;
        sm.is_audio = (type == AVMEDIA_TYPE_AUDIO);

        // ── Decoder ──────────────────────────────────────────────────────────
        const AVCodec* dec = avcodec_find_decoder(in_st->codecpar->codec_id);
        if (!dec) continue;
        sm.dec_ctx = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(sm.dec_ctx, in_st->codecpar);
        sm.dec_ctx->thread_count = 2;
        if (avcodec_open2(sm.dec_ctx, dec, nullptr) < 0) {
            avcodec_free_context(&sm.dec_ctx); continue;
        }

        // ── Encoder ──────────────────────────────────────────────────────────
        sm.out_st = avformat_new_stream(out_fmt, nullptr);

        if (sm.is_audio) {
            // Determine output audio codec
            enum AVCodecID acodec_id = AV_CODEC_ID_MP3;
            std::string ext = fs::path(job.output_path).extension().string();
            if (ext==".flac")      acodec_id = AV_CODEC_ID_FLAC;
            else if (ext==".wav")  acodec_id = AV_CODEC_ID_PCM_S16LE;
            else if (ext==".ogg")  acodec_id = AV_CODEC_ID_VORBIS;
            else if (ext==".aac" || ext==".m4a") acodec_id = AV_CODEC_ID_AAC;
            else if (ext==".opus") acodec_id = AV_CODEC_ID_OPUS;
            else if (ext==".mp4" || ext==".mkv" || ext==".mov") acodec_id = AV_CODEC_ID_AAC;

            const AVCodec* enc = avcodec_find_encoder(acodec_id);
            if (!enc) { avcodec_free_context(&sm.dec_ctx); continue; }
            sm.enc_ctx = avcodec_alloc_context3(enc);

            sm.enc_ctx->sample_rate = sm.dec_ctx->sample_rate;
            sm.enc_ctx->bit_rate    = job.audio_bitrate;
            // Choose compatible channel layout
            if (enc->ch_layouts) {
                sm.enc_ctx->ch_layout = enc->ch_layouts[0];
            } else {
                sm.enc_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            }
            // Choose compatible sample format
            if (enc->sample_fmts) sm.enc_ctx->sample_fmt = enc->sample_fmts[0];
            else                  sm.enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
            sm.enc_ctx->time_base = {1, sm.enc_ctx->sample_rate};

            if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER)
                sm.enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (avcodec_open2(sm.enc_ctx, enc, nullptr) < 0) {
                avcodec_free_context(&sm.dec_ctx);
                avcodec_free_context(&sm.enc_ctx); continue;
            }
            avcodec_parameters_from_context(sm.out_st->codecpar, sm.enc_ctx);
            sm.out_st->time_base = sm.enc_ctx->time_base;

            // Resampler
            sm.swr = swr_alloc();
            av_opt_set_chlayout  (sm.swr, "in_chlayout",  &sm.dec_ctx->ch_layout, 0);
            av_opt_set_int       (sm.swr, "in_sample_rate",  sm.dec_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(sm.swr, "in_sample_fmt",   sm.dec_ctx->sample_fmt, 0);
            av_opt_set_chlayout  (sm.swr, "out_chlayout", &sm.enc_ctx->ch_layout, 0);
            av_opt_set_int       (sm.swr, "out_sample_rate", sm.enc_ctx->sample_rate, 0);
            av_opt_set_sample_fmt(sm.swr, "out_sample_fmt",  sm.enc_ctx->sample_fmt, 0);
            swr_init(sm.swr);

        } else {
            // Video: copy stream (no re-encode for speed/quality, unless resize requested)
            bool needs_reencode = (job.width > 0 || job.height > 0);
            if (!needs_reencode) {
                avcodec_parameters_copy(sm.out_st->codecpar, in_st->codecpar);
                sm.out_st->codecpar->codec_tag = 0;
                sm.out_st->time_base = in_st->time_base;
                avcodec_free_context(&sm.dec_ctx);
                sm.dec_ctx = nullptr; sm.enc_ctx = nullptr;
            } else {
                const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
                if (!enc) { avcodec_free_context(&sm.dec_ctx); continue; }
                sm.enc_ctx = avcodec_alloc_context3(enc);
                sm.enc_ctx->width     = job.width  > 0 ? job.width  : sm.dec_ctx->width;
                sm.enc_ctx->height    = job.height > 0 ? job.height : sm.dec_ctx->height;
                sm.enc_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
                sm.enc_ctx->bit_rate  = job.video_bitrate;
                sm.enc_ctx->time_base = in_st->time_base;
                sm.enc_ctx->framerate = av_guess_frame_rate(in_fmt, in_st, nullptr);
                if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER)
                    sm.enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                avcodec_open2(sm.enc_ctx, enc, nullptr);
                avcodec_parameters_from_context(sm.out_st->codecpar, sm.enc_ctx);
                sm.out_st->time_base = sm.enc_ctx->time_base;
                sm.sws = sws_getContext(
                    sm.dec_ctx->width, sm.dec_ctx->height, sm.dec_ctx->pix_fmt,
                    sm.enc_ctx->width, sm.enc_ctx->height, AV_PIX_FMT_YUV420P,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
            }
        }
        maps.push_back(std::move(sm));
    }

    if (maps.empty()) return fail("No usable streams found");

    // ── Open output file ──────────────────────────────────────────────────────
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, job.output_path.c_str(), AVIO_FLAG_WRITE) < 0)
            return fail("Cannot open output file: " + job.output_path);
    }
    avformat_write_header(out_fmt, nullptr);

    // ── Transcode loop ────────────────────────────────────────────────────────
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    AVFrame*  conv  = av_frame_alloc();

    auto flushEncoder = [&](StreamMap& sm) {
        if (!sm.enc_ctx) return;
        avcodec_send_frame(sm.enc_ctx, nullptr);
        while (true) {
            av_packet_unref(pkt);
            if (avcodec_receive_packet(sm.enc_ctx, pkt) < 0) break;
            pkt->stream_index = (int)(sm.out_st - out_fmt->streams[0]);
            av_packet_rescale_ts(pkt, sm.enc_ctx->time_base, sm.out_st->time_base);
            av_interleaved_write_frame(out_fmt, pkt);
        }
    };

    while (!cancel_.load() && av_read_frame(in_fmt, pkt) >= 0) {
        int si = pkt->stream_index;
        // Find matching map
        StreamMap* sm = nullptr;
        for (auto& m : maps) if (m.in_idx == si) { sm = &m; break; }
        if (!sm) { av_packet_unref(pkt); continue; }

        // Update progress
        double pts_s = pkt->pts * av_q2d(in_fmt->streams[si]->time_base);
        prog.current_time = pts_s;
        prog.progress     = prog.duration > 0 ? pts_s / prog.duration : 0.0;
        cb(prog);

        // If no decoder (stream copy), just mux the packet directly
        if (!sm->dec_ctx) {
            av_packet_rescale_ts(pkt,
                in_fmt->streams[si]->time_base, sm->out_st->time_base);
            pkt->stream_index = (int)(sm->out_st - out_fmt->streams[0]);
            av_interleaved_write_frame(out_fmt, pkt);
            av_packet_unref(pkt);
            continue;
        }

        avcodec_send_packet(sm->dec_ctx, pkt);
        av_packet_unref(pkt);

        while (avcodec_receive_frame(sm->dec_ctx, frame) == 0) {
            AVFrame* enc_frame = frame;

            // Resample audio if needed
            if (sm->is_audio && sm->swr) {
                int out_samples = (int)av_rescale_rnd(
                    swr_get_delay(sm->swr, sm->dec_ctx->sample_rate) + frame->nb_samples,
                    sm->enc_ctx->sample_rate, sm->dec_ctx->sample_rate, AV_ROUND_UP);
                av_frame_unref(conv);
                conv->format         = sm->enc_ctx->sample_fmt;
                conv->sample_rate    = sm->enc_ctx->sample_rate;
                conv->nb_samples     = out_samples;
                av_channel_layout_copy(&conv->ch_layout, &sm->enc_ctx->ch_layout);
                av_frame_get_buffer(conv, 0);
                swr_convert(sm->swr,
                    conv->data, out_samples,
                    (const uint8_t**)frame->data, frame->nb_samples);
                conv->pts = av_rescale_q(frame->pts,
                    sm->dec_ctx->time_base, sm->enc_ctx->time_base);
                enc_frame = conv;
            }

            // Scale video if needed
            if (!sm->is_audio && sm->sws) {
                av_frame_unref(conv);
                conv->format = AV_PIX_FMT_YUV420P;
                conv->width  = sm->enc_ctx->width;
                conv->height = sm->enc_ctx->height;
                av_frame_get_buffer(conv, 0);
                sws_scale(sm->sws,
                    (const uint8_t* const*)frame->data, frame->linesize,
                    0, sm->dec_ctx->height,
                    conv->data, conv->linesize);
                conv->pts = av_rescale_q(frame->pts,
                    sm->dec_ctx->time_base, sm->enc_ctx->time_base);
                enc_frame = conv;
            }

            avcodec_send_frame(sm->enc_ctx, enc_frame);
            while (avcodec_receive_packet(sm->enc_ctx, pkt) == 0) {
                pkt->stream_index = (int)(sm->out_st - out_fmt->streams[0]);
                av_packet_rescale_ts(pkt, sm->enc_ctx->time_base, sm->out_st->time_base);
                av_interleaved_write_frame(out_fmt, pkt);
                av_packet_unref(pkt);
            }
            av_frame_unref(frame);
        }
    }

    // Flush
    for (auto& sm : maps) flushEncoder(sm);
    av_write_trailer(out_fmt);

    // ── Cleanup ───────────────────────────────────────────────────────────────
    av_frame_free(&frame);
    av_frame_free(&conv);
    av_packet_free(&pkt);
    for (auto& sm : maps) {
        if (sm.dec_ctx) avcodec_free_context(&sm.dec_ctx);
        if (sm.enc_ctx) avcodec_free_context(&sm.enc_ctx);
        if (sm.swr)     swr_free(&sm.swr);
        if (sm.sws)     sws_freeContext(sm.sws);
    }
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);

    if (!cancel_.load()) {
        prog.progress = 1.0; prog.done = true;
        cb(prog);
    }
    running_.store(false);
}
