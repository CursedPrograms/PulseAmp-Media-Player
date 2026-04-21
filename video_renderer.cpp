// ─── video_renderer.cpp ───────────────────────────────────────────────────────
#include "video_renderer.h"
#include <cstring>
#include <iostream>

void VideoRenderer::init(int w, int h) {
    destroy();
    tex_w_ = w; tex_h_ = h;
    glGenTextures(1, &texture_id_);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Allocate texture storage
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderer::destroy() {
    if (texture_id_) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    tex_w_ = tex_h_ = 0;
}

void VideoRenderer::uploadFrame(const VideoFrame& frame) {
    if (!texture_id_) return;
    if (frame.width != tex_w_ || frame.height != tex_h_) {
        init(frame.width, frame.height);
    }
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
