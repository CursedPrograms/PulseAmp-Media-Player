// ─── online.cpp ───────────────────────────────────────────────────────────────
#include "online.h"
#include "paths.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ─── Run a program, capture stdout / stderr ───────────────────────────────────
namespace {

struct ProcResult {
    int         exit_code = -1;
    std::string out, err;
};

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Quote one argument the way the MSVC runtime / Python parse it back
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; ++i) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { ++i; ++bs; }
        if (i == a.size()) { q.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"')  { q.append(bs * 2 + 1, L'\\'); q += L'"'; }
        else               { q.append(bs, L'\\'); q += a[i]; }
    }
    return q + L"\"";
}

std::string readAll(HANDLE h) {
    std::string s;
    char buf[4096];
    DWORD n;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) s.append(buf, n);
    return s;
}

ProcResult runProcess(const std::vector<std::string>& args) {
    ProcResult r;
    std::wstring cmd;
    for (auto& a : args) { if (!cmd.empty()) cmd += L' '; cmd += quoteArg(widen(a)); }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE out_r, out_w, err_r, err_w;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return r;
    if (!CreatePipe(&err_r, &err_w, &sa, 0)) { CloseHandle(out_r); CloseHandle(out_w); return r; }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError  = err_w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) {
        CloseHandle(out_r); CloseHandle(err_r);
        r.err = "could not start " + args[0];
        return r;
    }
    // Read stderr on a second thread so neither pipe can fill up and block
    std::thread err_th([&] { r.err = readAll(err_r); });
    r.out = readAll(out_r);
    err_th.join();
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = (int)code;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(out_r); CloseHandle(err_r);
    return r;
}
#else
std::string readAll(int fd) {
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) s.append(buf, (size_t)n);
    return s;
}

ProcResult runProcess(const std::vector<std::string>& args) {
    ProcResult r;
    int out_p[2], err_p[2];
    if (pipe(out_p) != 0) return r;
    if (pipe(err_p) != 0) { close(out_p[0]); close(out_p[1]); return r; }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(out_p[1], 1); dup2(err_p[1], 2);
        close(out_p[0]); close(out_p[1]); close(err_p[0]); close(err_p[1]);
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(out_p[1]); close(err_p[1]);
    if (pid < 0) { close(out_p[0]); close(err_p[0]); return r; }
    std::thread err_th([&] { r.err = readAll(err_p[0]); });
    r.out = readAll(out_p[0]);
    err_th.join();
    close(out_p[0]); close(err_p[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}
#endif

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l);
    }
    return v;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        v.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return v;
}

double toSeconds(const std::string& s) {
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    return (end && end != s.c_str()) ? d : 0.0;
}

// Last "ERROR: ..." line from yt-dlp, or a generic message
std::string errorFrom(const ProcResult& r) {
    std::string msg;
    for (auto& l : lines(r.err))
        if (l.rfind("ERROR:", 0) == 0) msg = l.substr(7);
    if (msg.empty()) msg = r.err.empty() ? "yt-dlp failed" : lines(r.err).back();
    return msg;
}

// Flat JSON object of strings -> "Key: value\r\n" header block
// (only what yt-dlp prints for %(http_headers)j)
std::string jsonHeadersToHttp(const std::string& j) {
    std::string out;
    size_t i = 0;
    auto readStr = [&](std::string& s) -> bool {
        while (i < j.size() && j[i] != '"') ++i;
        if (i >= j.size()) return false;
        ++i;
        s.clear();
        while (i < j.size() && j[i] != '"') {
            if (j[i] == '\\' && i + 1 < j.size()) {
                char e = j[++i];
                s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            } else s += j[i];
            ++i;
        }
        ++i;
        return true;
    };
    std::string k, v;
    while (readStr(k) && readStr(v))
        if (k.find_first_of("\r\n") == std::string::npos && v.find_first_of("\r\n") == std::string::npos)
            out += k + ": " + v + "\r\n";
    return out;
}

} // namespace

// ─── URL helpers ──────────────────────────────────────────────────────────────
bool isUrl(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

bool isOnlinePageUrl(const std::string& s) {
    if (!isUrl(s)) return false;
    static const char* hosts[] = { "youtube.com/", "youtu.be/", "music.youtube.com/",
                                   "soundcloud.com/", "on.soundcloud.com/" };
    for (auto h : hosts)
        if (s.find(h) != std::string::npos) return true;
    return false;
}

// ─── OnlineService ────────────────────────────────────────────────────────────
OnlineService::~OnlineService() {
    join(search_th_); join(update_th_);
}

static std::string findYtdlp() {
#ifdef _WIN32
    const char* name = "yt-dlp.exe";
#else
    const char* name = "yt-dlp";
#endif
    std::error_code ec;
    fs::path local = fs::path(exeDir()) / name;
    if (fs::exists(local, ec)) return local.string();

    // Search PATH
    const char* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    for (auto& dir : split(path, sep)) {
        if (dir.empty()) continue;
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p, ec)) return p.string();
    }
    return {};
}

void OnlineService::rescan() {
    std::string p = findYtdlp();
    std::lock_guard lk(path_mu_);
    ytdlp_ = p;
}

void OnlineService::search(OnlineSource src, const std::string& query, int max_results) {
    join(search_th_);
    searching_ = true;
    std::string exe = ytdlpPath();
    std::string q = (src == OnlineSource::YouTube ? "ytsearch" : "scsearch")
                  + std::to_string(max_results) + ":" + query;
    search_th_ = std::thread([this, exe, q] {
        std::vector<OnlineResult> res;
        std::string err;
        if (exe.empty()) err = "yt-dlp not found";
        else {
            ProcResult r = runProcess({ exe, "--encoding", "utf-8", "--no-warnings",
                "--flat-playlist", "--print",
                "%(webpage_url,url)s\t%(title)s\t%(duration)s\t%(uploader,channel,creator)s", q });
            if (r.exit_code != 0) err = errorFrom(r);
            for (auto& l : lines(r.out)) {
                auto f = split(l, '\t');
                if (f.size() < 4 || !isUrl(f[0])) continue;
                OnlineResult o;
                o.url = f[0]; o.title = f[1];
                o.duration = toSeconds(f[2]);
                o.uploader = f[3] == "NA" ? "" : f[3];
                res.push_back(o);
            }
            if (!res.empty()) err.clear();
            else if (err.empty()) err = "No results";
        }
        std::lock_guard lk(mu_);
        search_results_ = std::move(res);
        search_error_ = err;
        search_done_ = true;
        searching_ = false;
    });
}

static constexpr auto CACHE_TTL = std::chrono::minutes(30);

bool OnlineService::isCached(const std::string& page_url, bool video) const {
    std::lock_guard lk(cache_mu_);
    auto it = cache_.find((video ? "v|" : "a|") + page_url);
    return it != cache_.end() && std::chrono::steady_clock::now() - it->second.at < CACHE_TTL;
}

ResolvedStream OnlineService::resolveBlocking(const std::string& page_url, bool video,
                                             std::string& err) const {
    const std::string key = (video ? "v|" : "a|") + page_url;
    {
        std::lock_guard lk(cache_mu_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (std::chrono::steady_clock::now() - it->second.at < CACHE_TTL) { err.clear(); return it->second.rs; }
            cache_.erase(it);
        }
    }

    ResolvedStream rs;
    rs.page_url = page_url;
    err.clear();
    std::string exe = ytdlpPath();
    if (exe.empty()) { err = "yt-dlp not found"; return rs; }

    // Video: YouTube serves video and audio as separate streams, so ask for
    // both (H.264 up to 1080p decodes cheaply); the player reads two inputs.
    // Falls back to a single combined file where a site offers one.
    const char* fmt = video
        ? "bv*[height<=?1080][vcodec^=avc1]+ba[ext=m4a]/bv*[height<=?1080]+ba/b"
        : "bestaudio/best";
    ProcResult r = runProcess({ exe, "--encoding", "utf-8", "--no-warnings",
        "--no-playlist", "-f", fmt,
        "--print", "%(title)s", "--print", "%(duration)s",
        "--print", "%(requested_formats.0.url,url)s",
        "--print", "%(requested_formats.1.url)s",
        "--print", "%(http_headers)j", page_url });
    auto l = lines(r.out);
    if (r.exit_code != 0 || l.size() < 3 || !isUrl(l[2])) {
        err = r.exit_code != 0 ? errorFrom(r) : "No playable stream found";
        return rs;
    }
    rs.title      = l[0];
    rs.duration   = toSeconds(l[1]);
    rs.stream_url = l[2];
    if (l.size() > 3 && isUrl(l[3])) rs.audio_url = l[3];
    if (l.size() > 4) rs.http_headers = jsonHeadersToHttp(l[4]);
    if (rs.http_headers.empty())   // some sites refuse clients without a browser UA
        rs.http_headers = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0 Safari/537.36\r\n";

    std::lock_guard lk(cache_mu_);
    cache_[key] = { rs, std::chrono::steady_clock::now() };
    return rs;
}

void OnlineService::update() {
    join(update_th_);
    updating_ = true;
    std::string exe = ytdlpPath();
    update_th_ = std::thread([this, exe] {
        std::string msg;
        if (exe.empty()) msg = "yt-dlp not found";
        else {
            ProcResult r = runProcess({ exe, "--encoding", "utf-8", "-U" });
            auto l = lines(r.out + r.err);
            msg = r.exit_code == 0 ? (l.empty() ? "Up to date" : l.back()) : errorFrom(r);
        }
        std::lock_guard lk(mu_);
        update_msg_ = msg;
        update_done_ = true;
        updating_ = false;
    });
}

bool OnlineService::pollSearch(std::vector<OnlineResult>& out, std::string& error) {
    std::lock_guard lk(mu_);
    if (!search_done_) return false;
    search_done_ = false;
    out = std::move(search_results_);
    error = search_error_;
    return true;
}

bool OnlineService::pollUpdate(std::string& message) {
    std::lock_guard lk(mu_);
    if (!update_done_) return false;
    update_done_ = false;
    message = update_msg_;
    return true;
}
