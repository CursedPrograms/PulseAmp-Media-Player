// ─── media_cache.cpp ──────────────────────────────────────────────────────────
#include "media_cache.h"
#include "skin.h"
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#include <cstring>

namespace fs = std::filesystem;

static constexpr int    kMaxSide    = 256;   // thumbnails are downscaled to this
static constexpr size_t kMaxEntries = 400;

static bool isUrl(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

MediaCache::MediaCache()  { th_ = std::thread(&MediaCache::worker, this); }

MediaCache::~MediaCache() {
    { std::lock_guard lk(mu_); quit_ = true; }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    for (auto& [k, e] : entries_) if (e.info.tex) glDeleteTextures(1, &e.info.tex);
    if (placeholder_) glDeleteTextures(1, &placeholder_);
}

const MediaCache::Info* MediaCache::get(const std::string& source) {
    if (source.empty()) return nullptr;
    std::lock_guard lk(mu_);
    auto [it, inserted] = entries_.try_emplace(source);
    it->second.last_use = frame_;
    if (inserted) {
        queue_.push_front(source);   // newest first: what's on screen now loads first
        cv_.notify_one();
    }
    return &it->second.info;
}

const MediaCache::Info* MediaCache::fromBytes(const std::string& key, const std::vector<uint8_t>& bytes) {
    std::lock_guard lk(mu_);
    auto [it, inserted] = entries_.try_emplace("bytes:" + key);
    Entry& e = it->second;
    e.last_use = frame_;
    if (inserted) {
        SkinImage img;
        if (!bytes.empty() && decodeImageData(bytes.data(), bytes.size(), img)) {
            e.rgba = std::move(img.rgba);
            e.info.w = img.w; e.info.h = img.h;
            e.upload = true;
        }
        e.info.loaded = true;
    }
    return &e.info;
}

// Downscale RGBA to at most kMaxSide on the longest side
static void shrink(SkinImage& img) {
    int w = img.w, h = img.h;
    if (w <= kMaxSide && h <= kMaxSide) return;
    float s = (float)kMaxSide / std::max(w, h);
    int nw = std::max(1, (int)(w * s)), nh = std::max(1, (int)(h * s));
    SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_RGBA, nw, nh, AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr);
    if (!sws) return;
    // The source also gets aligned, padded rows: swscale reads whole blocks too
    uint8_t* src[4] = {};
    int src_ls[4] = {};
    if (av_image_alloc(src, src_ls, w, h + 1, AV_PIX_FMT_RGBA, 64) < 0) { sws_freeContext(sws); return; }
    for (int y = 0; y < h; ++y)
        std::memcpy(src[0] + (size_t)src_ls[0] * y, img.rgba.data() + (size_t)w * 4 * y, (size_t)w * 4);
    std::vector<uint8_t> out;
    const bool ok = swsToRGBA(sws, src, src_ls, h, nw, nh, out);
    av_freep(&src[0]);
    sws_freeContext(sws);
    if (!ok) return;
    img.rgba = std::move(out);
    img.w = nw; img.h = nh;
}

void MediaCache::load(const std::string& source, Entry& out) {
    SkinImage img;
    bool have = false;

    if (isUrl(source)) {
        // Online thumbnail: download the image
        AVIOContext* io = nullptr;
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "rw_timeout", "10000000", 0);
        av_dict_set(&opts, "user_agent", "Mozilla/5.0", 0);
        if (avio_open2(&io, source.c_str(), AVIO_FLAG_READ, nullptr, &opts) >= 0) {
            std::vector<uint8_t> data;
            uint8_t buf[16384];
            int n;
            while ((n = avio_read(io, buf, sizeof(buf))) > 0 && data.size() < 8u * 1024 * 1024)
                data.insert(data.end(), buf, buf + n);
            avio_closep(&io);
            have = decodeImageData(data.data(), data.size(), img);
        }
        av_dict_free(&opts);
    } else {
        // Local file: tags + embedded cover art
        AVFormatContext* ctx = nullptr;
        if (avformat_open_input(&ctx, source.c_str(), nullptr, nullptr) >= 0) {
            avformat_find_stream_info(ctx, nullptr);
            auto tag = [&](const char* k) -> std::string {
                if (auto* e = av_dict_get(ctx->metadata, k, nullptr, 0)) return e->value;
                for (unsigned s = 0; s < ctx->nb_streams; ++s)
                    if (auto* e = av_dict_get(ctx->streams[s]->metadata, k, nullptr, 0)) return e->value;
                return {};
            };
            out.info.title  = tag("title");
            out.info.artist = tag("artist");
            if (out.info.artist.empty()) out.info.artist = tag("album_artist");
            out.info.album  = tag("album");
            out.info.duration = ctx->duration > 0 ? (double)ctx->duration / AV_TIME_BASE : 0.0;
            for (unsigned s = 0; s < ctx->nb_streams && !have; ++s) {
                const AVStream* st = ctx->streams[s];
                if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) && st->attached_pic.size > 0)
                    have = decodeImageData(st->attached_pic.data, st->attached_pic.size, img);
            }
            avformat_close_input(&ctx);
        }
        // Folder art
        if (!have) {
            std::error_code ec;
            fs::path dir = fs::u8path(source).parent_path();
            for (const char* name : { "cover.jpg", "folder.jpg", "cover.png", "folder.png", "front.jpg", "album.jpg" }) {
                fs::path p = dir / name;
                if (!fs::exists(p, ec)) continue;
                std::ifstream f(p, std::ios::binary);
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
                if ((have = decodeImageData(data.data(), data.size(), img))) break;
            }
        }
    }

    if (have) {
        shrink(img);
        out.rgba = std::move(img.rgba);
        out.info.w = img.w; out.info.h = img.h;
        out.upload = true;
    }
    out.info.loaded = true;
}

void MediaCache::worker() {
    for (;;) {
        std::string source;
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [&] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            source = queue_.front();
            queue_.pop_front();
            // Skip entries that were evicted meanwhile
            if (!entries_.count(source)) continue;
        }
        Entry loaded;
        load(source, loaded);
        std::lock_guard lk(mu_);
        auto it = entries_.find(source);
        if (it == entries_.end()) continue;
        it->second.pending = loaded.info;
        it->second.rgba = std::move(loaded.rgba);
        it->second.upload = loaded.upload;
        it->second.pending_ready = true;
    }
}

void MediaCache::update() {
    std::lock_guard lk(mu_);
    ++frame_;
    for (auto& [k, e] : entries_) {
        if (e.pending_ready) {
            unsigned tex = e.info.tex;
            e.info = e.pending;
            e.info.tex = tex;
            e.pending_ready = false;
        }
        if (!e.upload) continue;
        e.upload = false;
        if (!e.info.tex) glGenTextures(1, &e.info.tex);
        glBindTexture(GL_TEXTURE_2D, e.info.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, e.info.w, e.info.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.rgba.data());
        e.rgba.clear();
        e.rgba.shrink_to_fit();
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Evict the least recently used entries
    if (entries_.size() > kMaxEntries) {
        std::vector<std::pair<uint64_t, std::string>> order;
        for (auto& [k, e] : entries_) order.push_back({ e.last_use, k });
        std::sort(order.begin(), order.end());
        for (size_t i = 0; i < entries_.size() - kMaxEntries && i < order.size(); ++i) {
            auto it = entries_.find(order[i].second);
            if (it->second.last_use + 2 >= frame_) break;   // in use right now
            if (it->second.info.tex) glDeleteTextures(1, &it->second.info.tex);
            entries_.erase(it);
        }
    }
}

void MediaCache::releaseGL() {
    std::lock_guard lk(mu_);
    for (auto& [k, e] : entries_) if (e.info.tex) { glDeleteTextures(1, &e.info.tex); e.info.tex = 0; }
    entries_.clear();
    queue_.clear();
    if (placeholder_) { glDeleteTextures(1, &placeholder_); placeholder_ = 0; }
}

unsigned MediaCache::placeholder() {
    if (placeholder_) return placeholder_;
    // Diagonal pink -> violet -> cyan gradient with a soft glow (drawn once)
    const int n = 128;
    std::vector<uint8_t> px((size_t)n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float t = (x + y) / (2.f * (n - 1));
            float r = t < 0.5f ? 255 : 255 * (1 - (t - 0.5f) * 2) + 1 * (t - 0.5f) * 2;
            float g = t < 0.5f ? 60 + 0 * t : 60 + (220 - 60) * (t - 0.5f) * 2;
            float b = 200 + 55 * t;
            float d = std::hypot(x - n * 0.5f, y - n * 0.5f) / (n * 0.7f);
            float k = 0.35f + 0.45f * std::max(0.f, 1 - d);
            uint8_t* p = &px[((size_t)y * n + x) * 4];
            p[0] = (uint8_t)(r * k); p[1] = (uint8_t)(g * k); p[2] = (uint8_t)(b * k); p[3] = 255;
        }
    glGenTextures(1, &placeholder_);
    glBindTexture(GL_TEXTURE_2D, placeholder_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return placeholder_;
}
