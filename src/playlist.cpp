// ─── playlist.cpp ─────────────────────────────────────────────────────────────
#include "playlist.h"
#include <algorithm>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <ctime>

namespace fs = std::filesystem;

Playlist::Playlist() {
    // Determine resume file location
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    resume_path_ = appdata ? std::string(appdata) + "\\NovPlayer\\resume.dat"
                           : "resume.dat";
#else
    const char* home = std::getenv("HOME");
    resume_path_ = home ? std::string(home) + "/.novplayer/resume.dat"
                        : "resume.dat";
#endif
    // Create directory if needed
    try {
        fs::create_directories(fs::path(resume_path_).parent_path());
    } catch (...) {}
    loadResume();
}

void Playlist::addFile(const std::string& path, const std::string& title, double dur) {
    PlaylistEntry e;
    e.path     = path;
    e.title    = title.empty() ? fs::path(path).stem().string() : title;
    e.duration = dur;
    entries_.push_back(e);
    order_.push_back((int)order_.size());
}

void Playlist::removeAt(int idx) {
    if (idx < 0 || idx >= (int)entries_.size()) return;
    entries_.erase(entries_.begin() + idx);
    order_.erase(std::remove(order_.begin(), order_.end(), idx), order_.end());
    // Remap indices
    for (auto& o : order_) if (o > idx) --o;
    if (current_ >= (int)order_.size()) current_ = std::max(0, (int)order_.size()-1);
}

void Playlist::clear() {
    entries_.clear(); order_.clear(); current_ = 0;
}

void Playlist::move(int from, int to) {
    if (from == to) return;
    auto it = entries_.begin();
    PlaylistEntry e = entries_[from];
    entries_.erase(it + from);
    entries_.insert(entries_.begin() + to, e);
}

const PlaylistEntry* Playlist::current() const {
    if (entries_.empty() || current_ < 0 || current_ >= (int)order_.size())
        return nullptr;
    int idx = order_[current_];
    if (idx < 0 || idx >= (int)entries_.size()) return nullptr;
    return &entries_[idx];
}

int Playlist::nextIdx() const {
    int n = (int)order_.size();
    if (n == 0) return -1;
    switch (repeat_) {
        case RepeatMode::One:  return current_;
        case RepeatMode::None: return (current_ + 1 < n) ? current_ + 1 : -1;
        case RepeatMode::All:  return (current_ + 1) % n;
    }
    return -1;
}

int Playlist::prevIdx() const {
    int n = (int)order_.size();
    if (n == 0) return -1;
    return (current_ - 1 + n) % n;
}

const PlaylistEntry* Playlist::next() {
    int i = nextIdx();
    if (i < 0) return nullptr;
    current_ = i;
    return current();
}

const PlaylistEntry* Playlist::prev() {
    current_ = prevIdx();
    return current();
}

void Playlist::setIndex(int i) {
    if (i >= 0 && i < (int)order_.size()) current_ = i;
}

void Playlist::setShuffle(bool s) {
    shuffle_ = s;
    if (s) {
        std::shuffle(order_.begin(), order_.end(), std::mt19937(std::random_device{}()));
    } else {
        std::iota(order_.begin(), order_.end(), 0);
    }
}

// ─── Smart Resume ─────────────────────────────────────────────────────────────
void Playlist::savePosition(const std::string& path, double seconds) {
    // Positions this close to the start aren't worth resuming (see getSavedPosition)
    if (seconds > 2.0) resume_map_[path] = seconds;
    else               resume_map_.erase(path);
}

std::optional<double> Playlist::getSavedPosition(const std::string& path) const {
    auto it = resume_map_.find(path);
    if (it != resume_map_.end() && it->second > 2.0) return it->second;
    return std::nullopt;
}

void Playlist::saveResume() const {
    std::ofstream f(resume_path_);
    if (!f) return;
    for (auto& [path, pos] : resume_map_)
        f << pos << "\t" << path << "\n";
}

void Playlist::loadResume() {
    std::ifstream f(resume_path_);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        try {
            double pos  = std::stod(line.substr(0, tab));
            std::string path = line.substr(tab + 1);
            resume_map_[path] = pos;
        } catch (const std::exception&) {
            // Skip a corrupt line instead of crashing at startup
        }
    }
}
