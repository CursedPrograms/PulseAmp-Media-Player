#pragma once
// ─── online.h ─────────────────────────────────────────────────────────────────
// YouTube / SoundCloud support via yt-dlp (https://github.com/yt-dlp/yt-dlp).
// yt-dlp turns a page URL into a direct media stream URL, which FFmpeg plays;
// it also provides search ("ytsearch:" / "scsearch:").
// yt-dlp is looked up next to the executable first, then on PATH.
// All calls run on a worker thread; results are collected with poll().
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class OnlineSource { YouTube, SoundCloud };

struct OnlineResult {             // one search hit
    std::string url;              // page URL (resolved again at play time)
    std::string title;
    std::string uploader;
    double      duration = 0.0;
};

struct ResolvedStream {           // ready to hand to the player
    std::string page_url;
    std::string stream_url;       // video+audio, video only, or audio only
    std::string audio_url;        // separate audio stream (YouTube video), else empty
    std::string title;
    std::string http_headers;     // "Name: value\r\n..." for FFmpeg's http protocol
    double      duration = 0.0;
};

// True for http(s) URLs of sites that need yt-dlp (vs. direct media URLs)
bool isOnlinePageUrl(const std::string& s);
bool isUrl(const std::string& s);

class OnlineService {
public:
    OnlineService() { rescan(); }
    ~OnlineService();

    // Path to yt-dlp, or empty if not installed (looked up once; rescan() to retry)
    std::string ytdlpPath() const { std::lock_guard lk(path_mu_); return ytdlp_; }
    bool available() const { return !ytdlpPath().empty(); }
    void rescan();

    // Start a background job (replaces any running one of the same kind)
    void search(OnlineSource src, const std::string& query, int max_results = 15);
    void update();                      // yt-dlp -U

    // Blocking (runs yt-dlp, can take 10+ seconds): call from a worker thread.
    // video=false asks for audio only. Results are cached for a while, so
    // replays and prefetched tracks start immediately.
    ResolvedStream resolveBlocking(const std::string& page_url, bool video,
                                   std::string& error) const;
    bool isCached(const std::string& page_url, bool video) const;

    // Main thread: collect finished work. Returns true if something changed.
    bool pollSearch(std::vector<OnlineResult>& out, std::string& error);
    bool pollUpdate(std::string& message);

    bool searching() const { return searching_.load(); }
    bool updating()  const { return updating_.load(); }

private:
    void join(std::thread& t) { if (t.joinable()) t.join(); }

    std::thread search_th_, update_th_;
    std::atomic<bool> searching_{false}, updating_{false};

    mutable std::mutex path_mu_;
    std::string ytdlp_;

    // Stream URLs expire (YouTube after hours, SoundCloud sooner): keep 30 min
    struct CacheEntry { ResolvedStream rs; std::chrono::steady_clock::time_point at; };
    mutable std::mutex cache_mu_;
    mutable std::map<std::string, CacheEntry> cache_;   // key: (video ? "v|" : "a|") + url

    std::mutex mu_;
    bool search_done_ = false, update_done_ = false;
    std::vector<OnlineResult> search_results_;
    std::string    search_error_, update_msg_;
};
