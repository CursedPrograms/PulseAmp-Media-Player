#pragma once
// ─── playlist.h ───────────────────────────────────────────────────────────────
// Playlist with shuffle, repeat, and smart resume.
// Smart resume: saves/restores playback position for every file ever played,
// stored in a plain text file (~/.novplayer/resume.dat).
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

struct PlaylistEntry {
    std::string path;
    std::string title;   // extracted from metadata or filename
    double      duration = 0.0;
};

enum class RepeatMode { None, One, All };

class Playlist {
public:
    Playlist();
    ~Playlist() { saveResume(); }

    // ── Management ────────────────────────────────────────────────────────────
    void addFile(const std::string& path, const std::string& title = "", double dur = 0.0);
    void removeAt(int idx);
    void clear();
    void move(int from, int to);

    // ── Navigation ────────────────────────────────────────────────────────────
    const PlaylistEntry* current() const;
    const PlaylistEntry* next();
    const PlaylistEntry* prev();
    void setIndex(int i);
    int  getIndex()  const { return current_; }
    int  size()      const { return (int)entries_.size(); }

    const std::vector<PlaylistEntry>& entries() const { return entries_; }

    // ── Shuffle / Repeat ──────────────────────────────────────────────────────
    void setShuffle(bool s);
    bool getShuffle()     const { return shuffle_; }
    void setRepeat(RepeatMode r) { repeat_ = r; }
    RepeatMode getRepeat()    const { return repeat_; }

    // ── Smart Resume ──────────────────────────────────────────────────────────
    void         savePosition(const std::string& path, double seconds);
    std::optional<double> getSavedPosition(const std::string& path) const;
    void         saveResume() const;
    void         loadResume();

private:
    int                 nextIdx() const;
    int                 prevIdx() const;

    std::vector<PlaylistEntry>           entries_;
    std::vector<int>                     order_;    // shuffled or sequential indices
    int                                  current_ = 0;
    bool                                 shuffle_ = false;
    RepeatMode                           repeat_  = RepeatMode::All;
    std::unordered_map<std::string, double> resume_map_;
    std::string                          resume_path_;
};
