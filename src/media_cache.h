#pragma once
// ─── media_cache.h ────────────────────────────────────────────────────────────
// Cover art thumbnails and tags for playlist / library / online entries,
// loaded on a worker thread:
//   • local files: embedded cover art (or cover.jpg / folder.jpg next to it)
//     plus title / artist / album / duration tags
//   • http(s) image URLs (online thumbnails): downloaded and decoded
// Textures are created on the main thread in update().
// ─────────────────────────────────────────────────────────────────────────────
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MediaCache {
public:
    struct Info {
        unsigned    tex = 0;          // GL texture (0: no art / not loaded yet)
        int         w = 0, h = 0;     // texture size
        std::string title, artist, album;
        double      duration = 0.0;
        bool        loaded = false;   // tags / art lookup finished
    };

    MediaCache();
    ~MediaCache();

    // Info for a media file path or an image URL; queues loading on first use.
    // The pointer stays valid until the next update().
    const Info* get(const std::string& source);

    // Art from encoded image bytes (e.g. the player's embedded cover), cached under key
    const Info* fromBytes(const std::string& key, const std::vector<uint8_t>& bytes);

    // Main thread, once per frame: upload finished images as textures
    void update();

    // Generic art (a neon gradient) for entries without a cover
    unsigned placeholder();

    // Delete all textures (GL context must be current); entries reload later
    void releaseGL();

private:
    // info is only touched on the main thread; the worker fills pending and
    // update() moves it into info
    struct Entry {
        Info info;
        Info pending;
        bool pending_ready = false;
        std::vector<uint8_t> rgba;
        bool upload = false;
        uint64_t last_use = 0;
    };
    void worker();
    void load(const std::string& source, Entry& out);   // runs on the worker

    std::map<std::string, Entry> entries_;
    std::deque<std::string>      queue_;
    std::mutex                   mu_;
    std::condition_variable      cv_;
    std::thread                  th_;
    bool                         quit_ = false;
    uint64_t                     frame_ = 0;
    unsigned                     placeholder_ = 0;
};
