// ─── milkdrop.cpp ─────────────────────────────────────────────────────────────
#include "milkdrop.h"
#include "player.h"
#include "paths.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

#ifdef PULSEAMP_HAVE_PROJECTM
#  ifdef PULSEAMP_USE_GLEW
#    include <GL/glew.h>          // Windows: GL 3 functions via GLEW (shared with projectM)
#  else
#    define GL_GLEXT_PROTOTYPES
#  endif
#  include <SDL2/SDL_opengl.h>
#  include <projectM-4/projectM.h>
#  include <projectM-4/playlist.h>
#endif

namespace fs = std::filesystem;

bool MilkDrop::compiledIn() {
#ifdef PULSEAMP_HAVE_PROJECTM
    return true;
#else
    return false;
#endif
}

#ifndef PULSEAMP_HAVE_PROJECTM
// ─── Stub (built without projectM) ────────────────────────────────────────────
MilkDrop::~MilkDrop() = default;
bool MilkDrop::init() { error_ = "This build of PulseAmp has no MilkDrop support"; return false; }
void MilkDrop::feed(const AudioRingBuffer&, float, int, bool) {}
void MilkDrop::renderToTexture(int, int) {}
int  MilkDrop::presetCount() const { return 0; }
std::string MilkDrop::presetName() const { return {}; }
void MilkDrop::next() {}
void MilkDrop::previous() {}
void MilkDrop::random() {}
void MilkDrop::setShuffle(bool) {}
void MilkDrop::setLocked(bool) {}
void MilkDrop::setDuration(double) {}
void MilkDrop::rescanPresets() {}

#else
// ─── projectM ─────────────────────────────────────────────────────────────────
#define PM  static_cast<projectm_handle>(handle_)
#define PL  static_cast<projectm_playlist_handle>(playlist_)

MilkDrop::~MilkDrop() {
    if (playlist_) projectm_playlist_destroy(PL);
    if (handle_)   projectm_destroy(PM);
    if (tex_)      glDeleteTextures(1, &tex_);
}

bool MilkDrop::init() {
    if (tried_) return ok();
    tried_ = true;

#ifdef PULSEAMP_USE_GLEW
    glewExperimental = GL_TRUE;   // needed for core profiles
    GLenum gerr = glewInit();
    if (gerr != GLEW_OK && gerr != GLEW_ERROR_NO_GLX_DISPLAY) {
        error_ = "OpenGL setup failed (GLEW)";
        return false;
    }
#endif

    handle_ = projectm_create();
    if (!handle_) {
        error_ = "projectM could not start (needs OpenGL 3.3)";
        return false;
    }
    projectm_set_preset_duration(PM, 30.0);
    projectm_set_soft_cut_duration(PM, 3.0);

    // Textures some presets use
    std::vector<std::string> tex_dirs = {
        (fs::path(exeDir()) / "textures").string(),
        (fs::path(appDataDir()) / "textures").string() };
    std::vector<const char*> tex_ptrs;
    for (auto& d : tex_dirs) tex_ptrs.push_back(d.c_str());
    projectm_set_texture_search_paths(PM, tex_ptrs.data(), tex_ptrs.size());

    playlist_ = projectm_playlist_create(PM);
    projectm_playlist_set_shuffle(PL, true);
    rescanPresets();
    return true;
}

void MilkDrop::rescanPresets() {
    if (!playlist_) return;
    projectm_playlist_clear(PL);
    dirs_ = { (fs::path(exeDir()) / "presets").string(),
              (fs::path(appDataDir()) / "presets").string() };
    std::error_code ec;
    fs::create_directories(dirs_[1], ec);
    for (auto& d : dirs_)
        if (fs::is_directory(d, ec))
            projectm_playlist_add_path(PL, d.c_str(), true, false);
    if (projectm_playlist_size(PL) > 0)
        projectm_playlist_play_next(PL, true);
    else
        error_ = "No MilkDrop presets found";
}

void MilkDrop::feed(const AudioRingBuffer& ring, float dt, int sample_rate, bool playing) {
    if (!ok()) return;
    const unsigned max = projectm_pcm_get_max_samples();
    unsigned frames = (unsigned)std::clamp(dt * (float)sample_rate, 0.f, 4096.f);
    frames = std::min(frames, max);
    if (frames == 0) return;
    pcm_.assign((size_t)frames * 2, 0.f);
    if (playing) ring.peekNext(pcm_.data(), (int)pcm_.size());
    projectm_pcm_add_float(PM, pcm_.data(), frames, PROJECTM_STEREO);
}

void MilkDrop::renderToTexture(int w, int h) {
    if (!ok() || w <= 0 || h <= 0) return;
    if (w != pm_w_ || h != pm_h_) {
        projectm_set_window_size(PM, (size_t)w, (size_t)h);
        pm_w_ = w; pm_h_ = h;
    }
    // projectM draws into the default framebuffer at (0,0,w,h)
    projectm_opengl_render_frame(PM);

    // Copy that into our texture
    if (!tex_) glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    if (w != tex_w_ || h != tex_h_) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        tex_w_ = w; tex_h_ = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int MilkDrop::presetCount() const {
    return playlist_ ? (int)projectm_playlist_size(PL) : 0;
}

std::string MilkDrop::presetName() const {
    if (!playlist_ || projectm_playlist_size(PL) == 0) return {};
    char* item = projectm_playlist_item(PL, projectm_playlist_get_position(PL));
    if (!item) return {};
    std::string name = fs::path(item).stem().string();
    projectm_playlist_free_string(item);
    return name;
}

void MilkDrop::next()     { if (presetCount()) projectm_playlist_play_next(PL, false); }
void MilkDrop::previous() { if (presetCount()) projectm_playlist_play_previous(PL, false); }
void MilkDrop::random() {
    if (int n = presetCount()) projectm_playlist_set_position(PL, (uint32_t)(std::rand() % n), false);
}
void MilkDrop::setShuffle(bool on)       { if (playlist_) projectm_playlist_set_shuffle(PL, on); }
void MilkDrop::setLocked(bool on)        { if (handle_) projectm_set_preset_locked(PM, on); }
void MilkDrop::setDuration(double s)     { if (handle_) projectm_set_preset_duration(PM, s); }

#endif
