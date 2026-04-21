#pragma once
// ─── video_renderer.h ─────────────────────────────────────────────────────────
// Uploads VideoFrames to an OpenGL RGBA texture for display via ImGui::Image.
// ─────────────────────────────────────────────────────────────────────────────
#include "player.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <memory>

class VideoRenderer {
public:
    VideoRenderer()  = default;
    ~VideoRenderer() { destroy(); }

    // Call once an OpenGL context exists
    void init(int w, int h);
    void destroy();

    // Upload a new frame to the GL texture
    void uploadFrame(const VideoFrame& frame);

    // ImGui-compatible texture handle
    void* getTextureID() const { return (void*)(intptr_t)texture_id_; }

    int getWidth()  const { return tex_w_; }
    int getHeight() const { return tex_h_; }

    bool isReady() const { return texture_id_ != 0; }

private:
    GLuint texture_id_ = 0;
    int    tex_w_ = 0, tex_h_ = 0;
};
