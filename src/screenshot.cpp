// ─── screenshot.cpp ───────────────────────────────────────────────────────────
#include "screenshot.h"
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

bool saveFramebufferPNG(const std::string& path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!codec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    ctx->width = w; ctx->height = h;
    ctx->pix_fmt = AV_PIX_FMT_RGBA;
    ctx->time_base = {1, 1};
    bool ok = false;
    AVFrame*  frame = av_frame_alloc();
    AVPacket* pkt   = av_packet_alloc();
    if (avcodec_open2(ctx, codec, nullptr) >= 0) {
        frame->format = AV_PIX_FMT_RGBA; frame->width = w; frame->height = h;
        if (av_frame_get_buffer(frame, 0) >= 0) {
            // OpenGL rows are bottom-up
            for (int y = 0; y < h; ++y)
                std::copy_n(&px[(size_t)(h - 1 - y) * w * 4], w * 4,
                            frame->data[0] + (size_t)y * frame->linesize[0]);
            if (avcodec_send_frame(ctx, frame) >= 0 && avcodec_receive_packet(ctx, pkt) >= 0) {
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    ok = std::fwrite(pkt->data, 1, pkt->size, f) == (size_t)pkt->size;
                    std::fclose(f);
                }
            }
        }
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return ok;
}
