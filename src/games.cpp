// ─── games.cpp ────────────────────────────────────────────────────────────────
#include "games.h"
#include "json.h"
#include "paths.h"
#include "skin_draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;
using namespace SkinDraw;

// ─── Helpers ──────────────────────────────────────────────────────────────────
namespace {

ImU32 hex(const std::string& s, ImU32 def) {
    if (s.size() != 7 && s.size() != 9) return def;
    if (s[0] != '#') return def;
    unsigned v = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return def;
        v = v * 16 + (unsigned)d;
    }
    if (s.size() == 7) v = (v << 8) | 0xff;
    return IM_COL32((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

ImU32 lerpCol(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int s) { return (int)(((a >> s) & 0xff) + (((int)((b >> s) & 0xff) - (int)((a >> s) & 0xff)) * t)); };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), ch(IM_COL32_A_SHIFT));
}

// A cheap glow for many small shapes (3 layers instead of glowRect's 7)
void softGlow(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float r, ImU32 col, float size) {
    for (int i = 3; i >= 1; --i) {
        const float e = size * i / 3.f;
        dl->AddRectFilled({p0.x - e, p0.y - e}, {p1.x + e, p1.y + e}, withAlpha(col, 0.07f), r + e);
    }
}

bool keyDown(ImGuiKey a, ImGuiKey b)    { return ImGui::IsKeyDown(a) || ImGui::IsKeyDown(b); }
bool keyPressed(ImGuiKey a, ImGuiKey b) { return ImGui::IsKeyPressed(a, false) || ImGui::IsKeyPressed(b, false); }

void hudText(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, const std::string& s, ImU32 glow = 0) {
    text(dl, fontFor(size), size, pos, col, s, glow);
}

ImVec2 hudSize(float size, const std::string& s) { return textSize(fontFor(size), size, s); }

} // namespace

// ─── Engine base ──────────────────────────────────────────────────────────────
class GameEngine {
public:
    explicit GameEngine(GameDef d) : def(std::move(d)) {}
    virtual ~GameEngine() = default;
    virtual void reset() = 0;
    virtual void update(float dt, const GameAudio& a) = 0;   // not called while paused or over
    virtual void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const GameAudio& a, double t) = 0;
    virtual const char* help() const = 0;

    GameDef def;
    int  score = 0;
    bool over  = false;

protected:
    std::mt19937 rng{ std::random_device{}() };
    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
    int   rndi(int a, int b)    { return std::uniform_int_distribution<int>(a, b)(rng); }
};

// ─── Snake ────────────────────────────────────────────────────────────────────
class Snake : public GameEngine {
public:
    using GameEngine::GameEngine;
    const char* help() const override { return "Arrows / WASD: steer"; }

    void reset() override {
        score = 0; over = false;
        if (def.map.empty()) {
            W = 32; H = 20;
            wall_.assign((size_t)W * H, 0);
        } else {
            H = (int)def.map.size();
            W = 0;
            for (auto& r : def.map) W = std::max(W, (int)r.size());
            W = std::max(W, 8); H = std::max(H, 6);
            wall_.assign((size_t)W * H, 0);
            for (int y = 0; y < (int)def.map.size(); ++y)
                for (int x = 0; x < (int)def.map[y].size(); ++x)
                    wall_[(size_t)y * W + x] = def.map[y][x] == '#';
        }
        // Start in the middle, heading right (clearing any wall there)
        body_.clear();
        const int cy = H / 2, cx = W / 2;
        for (int i = 0; i < 4; ++i) { body_.push_back({cx - i, cy}); wall_[(size_t)cy * W + cx - i] = 0; }
        dir_ = {1, 0};
        queue_.clear();
        acc_ = 0.f; grow_ = 0;
        bonus_life_ = 0.f;
        spawn(food_);
    }

    void update(float dt, const GameAudio& a) override {
        // Steering: queue up to 3 turns so quick presses aren't lost
        auto turn = [&](int dx, int dy) {
            Cell last = queue_.empty() ? dir_ : queue_.back();
            if ((last.x == -dx && last.y == -dy) || (last.x == dx && last.y == dy)) return;
            if (queue_.size() < 3) queue_.push_back({dx, dy});
        };
        if (keyPressed(ImGuiKey_LeftArrow,  ImGuiKey_A)) turn(-1, 0);
        if (keyPressed(ImGuiKey_RightArrow, ImGuiKey_D)) turn(1, 0);
        if (keyPressed(ImGuiKey_UpArrow,    ImGuiKey_W)) turn(0, -1);
        if (keyPressed(ImGuiKey_DownArrow,  ImGuiKey_S)) turn(0, 1);

        // Bonus notes drop in on the beat
        if (bonus_life_ > 0.f) bonus_life_ -= dt;
        if (def.beat && a.beat && bonus_life_ <= 0.f && rnd(0.f, 1.f) < 0.3f) { spawn(bonus_); bonus_life_ = 5.f; }

        const float interval = 0.12f / std::max(0.2f, def.speed) * std::max(0.55f, 1.f - body_.size() * 0.006f);
        acc_ += dt;
        while (acc_ >= interval && !over) { acc_ -= interval; step(); }
    }

    void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const GameAudio& a, double t) override {
        const float cs = std::floor(std::min((p1.x - p0.x) / W, (p1.y - p0.y - 40.f) / H));
        const ImVec2 o = {std::floor((p0.x + p1.x - cs * W) * 0.5f), std::floor(p0.y + 34.f + (p1.y - p0.y - 34.f - cs * H) * 0.5f)};
        auto at = [&](int x, int y) { return ImVec2(o.x + x * cs, o.y + y * cs); };

        // Arena: faint grid that flashes with the bass, glowing border
        const ImU32 grid = withAlpha(def.c3, 0.05f + a.bass * 0.10f);
        for (int x = 1; x < W; ++x) dl->AddLine(at(x, 0), at(x, H), grid);
        for (int y = 1; y < H; ++y) dl->AddLine(at(0, y), at(W, y), grid);
        glowRect(dl, at(0, 0), at(W, H), 6.f, def.c3, 6.f + a.bass * 8.f);
        dl->AddRect(at(0, 0), at(W, H), def.c3, 6.f, 0, 2.f);

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (wall_[(size_t)y * W + x]) {
                    ImVec2 q0 = at(x, y), q1 = at(x + 1, y + 1);
                    dl->AddRectFilled(q0, q1, withAlpha(def.c3, 0.35f), 2.f);
                    dl->AddRect(q0, q1, def.c3, 2.f);
                }

        // Food pulses with the bass; the bonus note blinks out when it's about to go
        auto orb = [&](Cell c, ImU32 col, float scale) {
            ImVec2 m = {o.x + (c.x + 0.5f) * cs, o.y + (c.y + 0.5f) * cs};
            const float r = cs * 0.32f * scale;
            glowCircle(dl, m, r, col, cs * 0.5f);
            dl->AddCircleFilled(m, r, col, 20);
        };
        orb(food_, def.c2, 1.f + a.bass * 0.5f);
        if (bonus_life_ > 0.f && (bonus_life_ > 1.5f || std::fmod(t, 0.2) < 0.1))
            orb(bonus_, def.c3, 1.2f + 0.2f * (float)std::sin(t * 10.0));

        // Body: gradient from the head, head glows
        const int n = (int)body_.size();
        for (int i = n - 1; i >= 0; --i) {
            const ImU32 col = lerpCol(def.c1, def.c3, n > 1 ? (float)i / (n - 1) : 0.f);
            const float in = cs * (i == 0 ? 0.04f : 0.12f);
            ImVec2 q0 = at(body_[i].x, body_[i].y), q1 = at(body_[i].x + 1, body_[i].y + 1);
            q0.x += in; q0.y += in; q1.x -= in; q1.y -= in;
            if (i == 0) softGlow(dl, q0, q1, cs * 0.3f, col, cs * 0.45f);
            dl->AddRectFilled(q0, q1, col, cs * 0.3f);
        }
    }

private:
    struct Cell { int x, y; };
    int W = 32, H = 20;
    std::vector<uint8_t> wall_;
    std::deque<Cell> body_, queue_;
    Cell dir_{1, 0}, food_{0, 0}, bonus_{0, 0};
    float acc_ = 0.f, bonus_life_ = 0.f;
    int grow_ = 0;

    bool occupied(int x, int y) const {
        if (wall_[(size_t)y * W + x]) return true;
        for (auto& c : body_) if (c.x == x && c.y == y) return true;
        return false;
    }
    void spawn(Cell& c) {
        for (int tries = 0; tries < 500; ++tries) {
            c = {rndi(0, W - 1), rndi(0, H - 1)};
            if (!occupied(c.x, c.y) && !(c.x == food_.x && c.y == food_.y && &c != &food_)) return;
        }
    }
    void step() {
        if (!queue_.empty()) { dir_ = queue_.front(); queue_.pop_front(); }
        Cell h = {body_.front().x + dir_.x, body_.front().y + dir_.y};
        if (def.wrap) { h.x = (h.x + W) % W; h.y = (h.y + H) % H; }
        else if (h.x < 0 || h.y < 0 || h.x >= W || h.y >= H) { over = true; return; }
        // The tail moves out of the way this step unless the snake is growing
        const Cell tail = body_.back();
        const bool into_tail = grow_ == 0 && h.x == tail.x && h.y == tail.y;
        if (occupied(h.x, h.y) && !into_tail) { over = true; return; }
        body_.push_front(h);
        if (h.x == food_.x && h.y == food_.y) { score += 10; grow_ += 3; spawn(food_); }
        if (bonus_life_ > 0.f && h.x == bonus_.x && h.y == bonus_.y) { score += 50; grow_ += 5; bonus_life_ = 0.f; }
        if (grow_ > 0) --grow_; else body_.pop_back();
    }
};

// ─── Neon Runner ──────────────────────────────────────────────────────────────
// World units: the play area is 10 units high; the ground is at 8.5.
class Runner : public GameEngine {
public:
    using GameEngine::GameEngine;
    const char* help() const override { return "Space / Up: jump (twice for a double jump, hold for higher)"; }

    void reset() override {
        score = 0; over = false;
        y_ = 0.f; vy_ = 0.f; jumps_ = 2;
        obs_.clear(); trail_.clear();
        speed_ = 8.f * def.speed;
        dist_ = 0.f; since_spawn_ = 0.f; cleared_ = 0;
    }

    void update(float dt, const GameAudio& a) override {
        speed_ = std::min(22.f * def.speed, speed_ + dt * 0.15f * def.speed);
        dist_ += speed_ * dt;

        // Jump: double jump, higher while held
        if (keyPressed(ImGuiKey_Space, ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_W, false))
            if (jumps_ > 0) { vy_ = 13.f; --jumps_; }
        const bool hold = keyDown(ImGuiKey_Space, ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_W);
        const float g = 38.f * def.gravity * (hold && vy_ > 0.f ? 0.55f : 1.f);
        y_ += vy_ * dt;
        vy_ -= g * dt;
        if (y_ <= 0.f) { y_ = 0.f; vy_ = 0.f; jumps_ = 2; }

        // Obstacles arrive on the beat (a timer takes over without music), at
        // least 7 units apart so there is room to land between them
        since_spawn_ += dt;
        const float min_gap = 7.f * def.gap / speed_;
        const bool on_beat = def.beat && a.beat && since_spawn_ >= min_gap;
        if (on_beat || since_spawn_ >= std::max(1.5f * def.gap, min_gap)) {
            since_spawn_ = 0.f;
            Obs o;
            o.x = world_w_ + 1.f;
            if (rnd(0.f, 1.f) < 0.2f) { o.air = true; o.w = rnd(2.5f, 4.f); o.y = rnd(1.35f, 1.6f); o.h = 0.6f; }
            else                      { o.w = rnd(0.8f, 1.6f); o.h = rnd(1.f, 2.6f); }
            obs_.push_back(o);
        }

        const float px = 2.f;
        for (auto& o : obs_) {
            o.x -= speed_ * dt;
            if (!o.passed && o.x + o.w < px) { o.passed = true; ++cleared_; }
            // Collision, a little forgiving
            const float k = 0.14f;
            const float ox0 = o.x + k, ox1 = o.x + o.w - k, oy0 = o.y + k, oy1 = o.y + o.h - k;
            if (px + 1.f - k > ox0 && px + k < ox1 && y_ + 1.f - k > oy0 && y_ + k < oy1) over = true;
        }
        obs_.erase(std::remove_if(obs_.begin(), obs_.end(), [](const Obs& o) { return o.x + o.w < -2.f; }), obs_.end());

        trail_.push_front(y_);
        if (trail_.size() > 10) trail_.pop_back();
        score = (int)dist_ + cleared_ * 25;
    }

    void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const GameAudio& a, double) override {
        const float u = (p1.y - p0.y) / 10.f;
        world_w_ = (p1.x - p0.x) / u;
        const float gy = p0.y + 8.5f * u;
        auto X = [&](float x) { return p0.x + x * u; };
        auto Y = [&](float y) { return gy - y * u; };

        // Ground: glowing line, dashes scrolling with the distance
        glowLine(dl, {p0.x, gy}, {p1.x, gy}, def.c1, 2.f, 8.f + a.bass * 10.f);
        dl->AddLine({p0.x, gy}, {p1.x, gy}, def.c1, 2.f);
        const float off = std::fmod(dist_, 2.f);
        for (float x = -off; x < world_w_; x += 2.f)
            dl->AddLine({X(x), gy + 0.35f * u}, {X(x + 0.8f), gy + 0.35f * u}, withAlpha(def.c1, 0.35f), 2.f);

        for (auto& o : obs_) {
            ImVec2 q0 = {X(o.x), Y(o.y + o.h)}, q1 = {X(o.x + o.w), Y(o.y)};
            softGlow(dl, q0, q1, 3.f, def.c2, u * 0.25f);
            dl->AddRectFilled(q0, q1, withAlpha(def.c2, 0.3f), 3.f);
            dl->AddRect(q0, q1, def.c2, 3.f, 0, 2.f);
        }

        // Player with a motion trail
        for (int i = (int)trail_.size() - 1; i >= 1; --i) {
            const float x = 2.f - i * 0.28f;
            dl->AddRectFilled({X(x), Y(trail_[i] + 1.f)}, {X(x + 1.f), Y(trail_[i])},
                              withAlpha(def.c3, 0.25f * (1.f - (float)i / trail_.size())), 4.f);
        }
        ImVec2 q0 = {X(2.f), Y(y_ + 1.f)}, q1 = {X(3.f), Y(y_)};
        glowRect(dl, q0, q1, 4.f, def.c3, u * 0.35f);
        dl->AddRectFilled(q0, q1, def.c3, 4.f);
    }

private:
    struct Obs { float x = 0, y = 0, w = 1, h = 1; bool air = false, passed = false; };
    std::vector<Obs> obs_;
    std::deque<float> trail_;
    float y_ = 0, vy_ = 0, speed_ = 8, dist_ = 0, since_spawn_ = 0, world_w_ = 18;
    int jumps_ = 2, cleared_ = 0;
};

// ─── Invaders ─────────────────────────────────────────────────────────────────
// World: 160 x 100 units, scaled to fit.
class Invaders : public GameEngine {
public:
    using GameEngine::GameEngine;
    const char* help() const override { return "Left / Right: move   Space: fire"; }

    void reset() override {
        score = 0; over = false;
        lives_ = std::max(1, def.lives);
        wave_ = 0;
        px_ = 80.f;
        startWave();
    }

    void update(float dt, const GameAudio& a) override {
        if (keyDown(ImGuiKey_LeftArrow, ImGuiKey_A))  px_ -= 75.f * dt;
        if (keyDown(ImGuiKey_RightArrow, ImGuiKey_D)) px_ += 75.f * dt;
        px_ = std::clamp(px_, 7.f, 153.f);
        fire_cd_ -= dt;
        if (ImGui::IsKeyDown(ImGuiKey_Space) && !shot_ && fire_cd_ <= 0.f) { shot_ = true; sx_ = px_; sy_ = 86.f; fire_cd_ = 0.3f; }
        hit_flash_ = std::max(0.f, hit_flash_ - dt);

        // The player's shot
        if (shot_) {
            sy_ -= 150.f * dt;
            if (sy_ < 0.f) shot_ = false;
            for (auto& v : inv_) {
                if (!v.alive) continue;
                const float x = fx_ + v.x, y = fy_ + v.y;
                if (std::fabs(sx_ - x) < 5.5f && std::fabs(sy_ - y) < 4.f) {
                    v.alive = false; shot_ = false;
                    score += v.type == 'S' ? 30 : v.type == 'B' ? 50 : 20;
                    burst(x, y, colorOf(v), 14);
                    break;
                }
            }
        }

        // March: on the beat with music, faster as the formation thins out
        int alive = 0;
        for (auto& v : inv_) alive += v.alive;
        if (alive == 0) { score += 100 * (wave_ + 1); ++wave_; startWave(); return; }
        const float interval = std::max(0.07f, 0.7f * alive / std::max<size_t>(1, inv_.size())) / std::max(0.2f, def.speed);
        since_march_ += dt;
        if (since_march_ >= interval || (def.beat && a.beat && since_march_ >= 0.1f)) { since_march_ = 0.f; march(); }

        // Bombs from the lowest alien of a random column
        bomb_timer_ -= dt;
        if (bomb_timer_ <= 0.f) {
            bomb_timer_ = rnd(0.4f, 1.4f) / (1.f + wave_ * 0.2f);
            const Alien* low = nullptr;
            const int start = rndi(0, (int)inv_.size() - 1);
            for (size_t k = 0; k < inv_.size() && !low; ++k) {
                const Alien& v = inv_[(start + k) % inv_.size()];
                if (!v.alive) continue;
                low = &v;
                for (auto& w : inv_) if (w.alive && std::fabs(w.x - v.x) < 1.f && w.y > low->y) low = &w;
            }
            if (low) bombs_.push_back({fx_ + low->x, fy_ + low->y + 4.f});
        }
        for (auto& b : bombs_) b.y += (38.f + wave_ * 4.f) * dt;
        for (auto& b : bombs_)
            if (b.y > 86.f && b.y < 96.f && std::fabs(b.x - px_) < 6.f) {
                b.y = 200.f;
                burst(px_, 91.f, def.c2, 24);
                hit_flash_ = 0.4f;
                if (--lives_ <= 0) over = true;
            }
        bombs_.erase(std::remove_if(bombs_.begin(), bombs_.end(), [](const Pt& b) { return b.y > 100.f; }), bombs_.end());

        for (auto& p : parts_) { p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt * 1.6f; }
        parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Part& p) { return p.life <= 0.f; }), parts_.end());
    }

    void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const GameAudio& a, double) override {
        const float s = std::min((p1.x - p0.x) / 160.f, (p1.y - p0.y - 30.f) / 100.f);
        const ImVec2 o = {(p0.x + p1.x - 160.f * s) * 0.5f, p0.y + 30.f + (p1.y - p0.y - 30.f - 100.f * s) * 0.5f};
        auto P = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };

        // Ground line and lives
        glowLine(dl, P(0, 97), P(160, 97), def.c2, 2.f, 6.f + a.bass * 8.f);
        dl->AddLine(P(0, 97), P(160, 97), def.c2, 2.f);

        // Aliens: pixel sprites, two frames, glow with the bass
        for (auto& v : inv_) {
            if (!v.alive) continue;
            const ImU32 col = colorOf(v);
            const char* const* spr = sprite(v.type, frame_);
            const int sw = (int)std::strlen(spr[0]);
            const float px = s * (v.type == 'B' ? 1.3f : 1.0f);
            const float x0 = o.x + (fx_ + v.x) * s - sw * px * 0.5f, y0 = o.y + (fy_ + v.y) * s - 4.f * px;
            softGlow(dl, {x0, y0}, {x0 + sw * px, y0 + 8 * px}, px, col, px * (0.6f + a.bass * 1.6f));
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < sw; ++c)
                    if (spr[r][c] == '#')
                        dl->AddRectFilled({x0 + c * px, y0 + r * px}, {x0 + (c + 1) * px, y0 + (r + 1) * px}, col);
        }

        // Player ship (blinks after a hit)
        if (hit_flash_ <= 0.f || std::fmod(hit_flash_, 0.1f) < 0.05f) {
            const ImVec2 pts[] = { P(px_, 86), P(px_ + 7, 94), P(px_ + 3, 94), P(px_, 92), P(px_ - 3, 94), P(px_ - 7, 94) };
            for (int i = 3; i >= 1; --i) dl->AddPolyline(pts, 6, withAlpha(def.c2, 0.12f), ImDrawFlags_Closed, s * (1.f + i * 1.5f));
            dl->AddConvexPolyFilled(pts, 3, def.c2);
            dl->AddPolyline(pts, 6, def.c2, ImDrawFlags_Closed, std::max(1.5f, s * 0.8f));
        }
        if (shot_) {
            glowLine(dl, P(sx_, sy_), P(sx_, sy_ + 5), def.c3, 2.f, 6.f);
            dl->AddLine(P(sx_, sy_), P(sx_, sy_ + 5), def.c3, std::max(2.f, s * 0.8f));
        }
        for (auto& b : bombs_) {
            ImVec2 z[4] = { P(b.x, b.y - 3), P(b.x + 1.2f, b.y - 1), P(b.x - 1.2f, b.y + 1), P(b.x, b.y + 3) };
            dl->AddPolyline(z, 4, def.c1, 0, std::max(1.5f, s * 0.6f));
        }
        for (auto& p : parts_)
            dl->AddRectFilled(P(p.x - 0.6f, p.y - 0.6f), P(p.x + 0.6f, p.y + 0.6f), withAlpha(p.col, p.life));

        // Lives
        for (int i = 0; i < lives_; ++i) {
            const ImVec2 c = P(150.f - i * 7.f, 99.5f);
            dl->AddTriangleFilled({c.x, c.y - 2.f * s}, {c.x + 2.5f * s, c.y + 1.f * s}, {c.x - 2.5f * s, c.y + 1.f * s}, withAlpha(def.c2, 0.8f));
        }
    }

private:
    struct Alien { float x, y; char type; bool alive; };
    struct Pt { float x, y; };
    struct Part { float x, y, vx, vy, life; ImU32 col; };
    std::vector<Alien> inv_;
    std::vector<Pt> bombs_;
    std::vector<Part> parts_;
    float fx_ = 0, fy_ = 0, dir_ = 1, px_ = 80, sx_ = 0, sy_ = 0;
    float since_march_ = 0, bomb_timer_ = 1, fire_cd_ = 0, hit_flash_ = 0;
    int lives_ = 3, wave_ = 0, frame_ = 0;
    bool shot_ = false;

    ImU32 colorOf(const Alien& v) const {
        return v.type == 'S' ? def.c1 : v.type == 'B' ? def.c3 : lerpCol(def.c1, def.c3, 0.35f + v.y / 60.f);
    }

    static const char* const* sprite(char type, int frame) {
        static const char* squid[2][8] = {
            { "...##...", "..####..", ".######.", "##.##.##", "########", "..#..#..", ".#.##.#.", "#.#..#.#" },
            { "...##...", "..####..", ".######.", "##.##.##", "########", ".#.##.#.", "#......#", ".#....#." } };
        static const char* crab[2][8] = {
            { "..#.....#..", "...#...#...", "..#######..", ".##.###.##.", "###########", "#.#######.#", "#.#.....#.#", "...##.##..." },
            { "..#.....#..", "#..#...#..#", "#.#######.#", "###.###.###", "###########", ".#########.", "..#.....#..", ".#.......#." } };
        static const char* boss[2][8] = {
            { "....####....", ".##########.", "############", "###..##..###", "############", "...##..##...", "..##.##.##..", "##........##" },
            { "....####....", ".##########.", "############", "###..##..###", "############", "..###..###..", ".##..##..##.", "..##....##.." } };
        return type == 'S' ? squid[frame] : type == 'B' ? boss[frame] : crab[frame];
    }

    void startWave() {
        static const std::vector<std::string> def_wave = { "SSSSSSSSSS", "CCCCCCCCCC", "CCCCCCCCCC", "CCCCCCCCCC" };
        const auto& rows = def.waves.empty() ? def_wave : def.waves[wave_ % def.waves.size()];
        inv_.clear();
        size_t cols = 0;
        for (auto& r : rows) cols = std::max(cols, r.size());
        cols = std::min<size_t>(cols, 12);
        for (size_t r = 0; r < rows.size() && r < 6; ++r)
            for (size_t c = 0; c < rows[r].size() && c < cols; ++c) {
                const char t = rows[r][c];
                if (t == 'S' || t == 'C' || t == 'B' || t == 'X')
                    inv_.push_back({ c * 12.f + 6.f, r * 10.f, t == 'X' ? 'C' : t, true });
            }
        fx_ = (160.f - cols * 12.f) * 0.5f;
        fy_ = 14.f + std::min(wave_, 4) * 3.f;
        dir_ = 1.f;
        shot_ = false;
        bombs_.clear();
        since_march_ = 0.f;
        bomb_timer_ = 1.5f;
    }

    void march() {
        frame_ ^= 1;
        float minx = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (auto& v : inv_) if (v.alive) { minx = std::min(minx, v.x); maxx = std::max(maxx, v.x); maxy = std::max(maxy, v.y); }
        const float step = 2.5f * dir_;
        if (fx_ + minx + step < 6.f || fx_ + maxx + step > 154.f) { fy_ += 4.f; dir_ = -dir_; }
        else fx_ += step;
        if (fy_ + maxy >= 82.f) over = true;   // they landed
    }

    void burst(float x, float y, ImU32 col, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = rnd(0.f, 6.2832f), v = rnd(10.f, 45.f);
            parts_.push_back({x, y, std::cos(a) * v, std::sin(a) * v, 1.f, col});
        }
    }
};

// ─── Killer Stardust ──────────────────────────────────────────────────────────
// A Cursed Arcade game (the web version: killer-stardust.netlify.app). Steer the comet, eat the stardust to grow
// your tail; invaders fall in waves, faster each wave, and drop in on the beat. One through your tail cuts it
// there (everything behind is lost); one on your head, or running into your own tail, and it's over.
class Stardust : public GameEngine {
public:
    using GameEngine::GameEngine;
    const char* help() const override { return "Arrows / WASD: steer"; }

    void reset() override {
        score = 0; over = false;
        W = 40; H = 24;
        body_.clear();
        for (int i = 0; i < 3; ++i) body_.push_back({W / 2 - i, H / 2});
        dir_ = {1, 0};
        queue_.clear();
        length_ = 3;
        dust_.clear();
        for (int i = 0; i < 36; ++i) dust_.push_back(freeCell());
        inv_.clear();
        cuts_.clear();
        wave_ = 1; wave_t_ = 0.f; acc_ = 0.f;
        spawnWave();
        stars_.clear();
        for (int i = 0; i < 160; ++i) stars_.push_back({rnd(0.f, 1.f), rnd(0.f, 1.f), rnd(0.f, 6.28f), rnd(0.5f, 2.5f)});
    }

    void update(float dt, const GameAudio& a) override {
        auto turn = [&](int dx, int dy) {
            Cell last = queue_.empty() ? dir_ : queue_.back();
            if ((last.x == -dx && last.y == -dy) || (last.x == dx && last.y == dy)) return;
            if (queue_.size() < 3) queue_.push_back({dx, dy});
        };
        if (keyPressed(ImGuiKey_LeftArrow,  ImGuiKey_A)) turn(-1, 0);
        if (keyPressed(ImGuiKey_RightArrow, ImGuiKey_D)) turn(1, 0);
        if (keyPressed(ImGuiKey_UpArrow,    ImGuiKey_W)) turn(0, -1);
        if (keyPressed(ImGuiKey_DownArrow,  ImGuiKey_S)) turn(0, 1);

        // waves on a clock, faster each time; with the music on, an extra invader drops in on some beats
        wave_t_ += dt;
        const float every = std::max(2.f, 6.f - wave_ * 0.4f) / std::max(0.25f, def.speed);
        if (wave_t_ > every) { wave_t_ = 0.f; ++wave_; spawnWave(); }
        if (def.beat && a.beat && rnd(0.f, 1.f) < 0.5f) inv_.push_back({(float)rndi(0, W - 1), -1.f, fallSpeed() * 1.4f});

        // the comet: a cell a step, a little quicker as it grows
        const float interval = 0.11f / std::max(0.25f, def.speed) * std::max(0.5f, 1.f - length_ * 0.004f);
        acc_ += dt;
        while (acc_ >= interval && !over) { acc_ -= interval; step(); }

        // the invaders fall; a head hit ends it, a tail hit cuts the tail there
        for (auto& v : inv_) {
            v.y += v.v * dt;
            if (v.y >= H) { v.y = -1.f; v.x = (float)rndi(0, W - 1); }
            const int vx = (int)std::lround(v.x), vy = (int)std::floor(v.y + 0.5f);
            if (vy < 0) continue;
            if (vx == body_.front().x && vy == body_.front().y) { over = true; return; }
            for (size_t i = 1; i < body_.size(); ++i) {
                if (body_[i].x == vx && body_[i].y == vy) {
                    cuts_.push_back({body_[i].x, body_[i].y, 0.45f});
                    body_.resize(i);
                    length_ = std::max(3, (int)i);
                    v.y = -1.f; v.x = (float)rndi(0, W - 1);
                    break;
                }
            }
        }
        for (auto& c : cuts_) c.life -= dt;
        cuts_.erase(std::remove_if(cuts_.begin(), cuts_.end(), [](const Cut& c) { return c.life <= 0.f; }), cuts_.end());
    }

    void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const GameAudio& a, double t) override {
        const float cs = std::floor(std::min((p1.x - p0.x) / W, (p1.y - p0.y - 40.f) / H));
        const ImVec2 o = {std::floor((p0.x + p1.x - cs * W) * 0.5f), std::floor(p0.y + 34.f + (p1.y - p0.y - 34.f - cs * H) * 0.5f)};
        auto at = [&](float x, float y) { return ImVec2(o.x + x * cs, o.y + y * cs); };

        // space: a dark field, stars that twinkle harder when the music's loud
        dl->AddRectFilled(at(0, 0), at((float)W, (float)H), IM_COL32(2, 6, 17, 210), 6.f);
        for (auto& s : stars_) {
            const float k = 0.35f + 0.65f * std::fabs(std::sin(s.t + (float)t * s.s * (1.f + a.energy * 2.f)));
            const ImVec2 q = at(s.x * W, s.y * H);
            dl->AddRectFilled(q, {q.x + 1.6f, q.y + 1.6f}, withAlpha(IM_COL32(255, 255, 255, 255), k));
        }
        glowRect(dl, at(0, 0), at((float)W, (float)H), 6.f, def.c1, 4.f + a.bass * 8.f);
        dl->AddRect(at(0, 0), at((float)W, (float)H), withAlpha(def.c1, 0.8f), 6.f, 0, 2.f);

        // the stardust (secondary colour), pulsing with the bass
        for (auto& d : dust_) {
            const ImVec2 m = at(d.x + 0.5f, d.y + 0.5f);
            const float r = cs * (0.17f + a.bass * 0.08f);
            glowCircle(dl, m, r, def.c2, cs * 0.45f);
            dl->AddCircleFilled(m, r, def.c2, 12);
        }

        // the comet: a white head, the tail in the primary colour, fading out
        const int n = (int)body_.size();
        for (int i = n - 1; i >= 0; --i) {
            const float fade = std::max(0.15f, 1.f - (float)i / (n + 2));
            const ImU32 col = i == 0 ? IM_COL32(255, 255, 255, 255) : withAlpha(def.c1, fade);
            ImVec2 q0 = at((float)body_[i].x, (float)body_[i].y), q1 = at(body_[i].x + 1.f, body_[i].y + 1.f);
            const float in = cs * 0.1f;
            q0.x += in; q0.y += in; q1.x -= in; q1.y -= in;
            softGlow(dl, q0, q1, cs * 0.2f, i == 0 ? def.c1 : col, cs * (i == 0 ? 0.6f : 0.35f));
            dl->AddRectFilled(q0, q1, col, cs * 0.18f);
        }

        // the invaders (accent colour): a little ship each
        for (auto& v : inv_) {
            if (v.y < -0.9f) continue;
            const ImVec2 q = at(v.x, v.y);
            const ImVec2 b0 = {q.x + cs * 0.15f, q.y + cs * 0.25f}, b1 = {q.x + cs * 0.85f, q.y + cs * 0.7f};
            softGlow(dl, b0, b1, 2.f, def.c3, cs * 0.35f);
            dl->AddRectFilled(b0, b1, def.c3, 2.f);
            dl->AddRectFilled({q.x + cs * 0.05f, q.y + cs * 0.45f}, {q.x + cs * 0.2f, q.y + cs * 0.85f}, def.c3);
            dl->AddRectFilled({q.x + cs * 0.8f, q.y + cs * 0.45f}, {q.x + cs * 0.95f, q.y + cs * 0.85f}, def.c3);
        }

        // where the tail was just cut: a ring that opens out
        for (auto& c : cuts_) {
            const float k = 1.f - c.life / 0.45f;
            dl->AddCircle(at(c.x + 0.5f, c.y + 0.5f), cs * (0.4f + k * 1.4f), withAlpha(def.c3, 1.f - k), 24, 3.f);
        }

        // the wave, top right
        const std::string w = "WAVE " + std::to_string(wave_);
        hudText(dl, {p1.x - hudSize(18.f, w).x - 14.f, p0.y + 8.f}, 18.f, def.c1, w);
    }

private:
    struct Cell { int x, y; };
    struct Inv { float x, y, v; };
    struct Cut { int x, y; float life; };
    struct Star { float x, y, t, s; };
    int W = 40, H = 24, length_ = 3, wave_ = 1;
    std::deque<Cell> body_, queue_;
    Cell dir_{1, 0};
    std::vector<Cell> dust_;
    std::vector<Inv> inv_;
    std::vector<Cut> cuts_;
    std::vector<Star> stars_;
    float acc_ = 0.f, wave_t_ = 0.f;

    float fallSpeed() { return (2.2f + wave_ * 0.35f) * std::max(0.25f, def.speed) * rnd(0.8f, 1.25f); }
    Cell freeCell() { return {rndi(0, W - 1), rndi(0, H - 1)}; }
    void spawnWave() {
        for (int i = 0; i < 3 + wave_ * 2; ++i) inv_.push_back({(float)rndi(0, W - 1), -rnd(1.f, 8.f), fallSpeed()});
    }
    void step() {
        if (!queue_.empty()) { dir_ = queue_.front(); queue_.pop_front(); }
        Cell h = {(body_.front().x + dir_.x + W) % W, (body_.front().y + dir_.y + H) % H};
        for (size_t i = 0; i + 1 < body_.size(); ++i)          // the very end moves away this step
            if (body_[i].x == h.x && body_[i].y == h.y) { over = true; return; }
        body_.push_front(h);
        for (auto& d : dust_)
            if (d.x == h.x && d.y == h.y) { score += 10; ++length_; d = freeCell(); }
        while ((int)body_.size() > length_) body_.pop_back();
    }
};

// ─── Arcade ───────────────────────────────────────────────────────────────────
Arcade::Arcade() = default;
Arcade::~Arcade() = default;

namespace {

GameDef builtin(const char* name, const char* engine, const char* c1, const char* c2, const char* c3) {
    GameDef d;
    d.name = name; d.engine = engine; d.author = "PulseAmp";
    d.c1 = hex(c1, IM_COL32_WHITE); d.c2 = hex(c2, IM_COL32_WHITE); d.c3 = hex(c3, IM_COL32_WHITE);
    return d;
}

std::vector<std::string> strings(const Json& arr) {
    std::vector<std::string> v;
    for (auto& s : arr.items()) if (s.isString()) v.push_back(s.str());
    return v;
}

// Read a .pag file; returns false with a message on error
bool loadPag(const fs::path& path, GameDef& d, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "can't read the file"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    const Json j = Json::parse(ss.str(), err);
    if (!j.isObject()) { if (err.empty()) err = "not a JSON object"; return false; }

    d.engine = j["engine"].str();
    const GameDef base = d.engine == "snake"    ? builtin("", "snake", "#00ff8c", "#ff3cc8", "#01dcff")
                       : d.engine == "runner"   ? builtin("", "runner", "#01dcff", "#ff3cc8", "#ffe14d")
                       : d.engine == "invaders" ? builtin("", "invaders", "#ff3cc8", "#00ff8c", "#01dcff")
                       : d.engine == "stardust" ? builtin("", "stardust", "#f088e9", "#34a9e3", "#39ff6a")
                       : GameDef{};
    if (base.engine.empty()) { err = "\"engine\" must be snake, runner, invaders or stardust"; return false; }

    d = base;
    d.path    = path.u8string();
    d.name    = j["name"].str(path.stem().u8string());
    d.author  = j["author"].str();
    const Json& c = j["colors"];
    d.c1      = hex(c["primary"].str(), base.c1);
    d.c2      = hex(c["secondary"].str(), base.c2);
    d.c3      = hex(c["accent"].str(), base.c3);
    d.speed   = std::clamp((float)j["speed"].num(1.0), 0.25f, 4.f);
    d.beat    = j["beat"].boolean(true);
    d.gap     = std::clamp((float)j["gap"].num(1.0), 0.3f, 4.f);
    d.gravity = std::clamp((float)j["gravity"].num(1.0), 0.3f, 3.f);
    d.lives   = std::clamp((int)j["lives"].num(3), 1, 9);
    d.wrap    = j["wrap"].boolean(true);
    d.map     = strings(j["map"]);
    for (auto& w : j["waves"].items()) {
        auto rows = strings(w);
        if (!rows.empty()) d.waves.push_back(rows);
    }
    return true;
}

} // namespace

void Arcade::scan() {
    defs_.clear();
    errors_.clear();
    defs_.push_back(builtin("Neon Snake", "snake",    "#00ff8c", "#ff3cc8", "#01dcff"));
    defs_.push_back(builtin("Neon Runner", "runner",  "#01dcff", "#ff3cc8", "#ffe14d"));
    defs_.push_back(builtin("Invaders", "invaders",   "#ff3cc8", "#00ff8c", "#01dcff"));
    defs_.push_back(builtin("Killer Stardust", "stardust", "#f088e9", "#34a9e3", "#39ff6a"));

    std::error_code ec;
    for (const fs::path dir : { fs::path(exeDir()) / "games", fs::path(appDataDir()) / "games" }) {
        std::vector<fs::path> files;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->path().extension() == ".pag") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (auto& p : files) {
            GameDef d;
            std::string err;
            if (loadPag(p, d, err)) defs_.push_back(d);
            else errors_.push_back(p.filename().u8string() + ": " + err);
        }
        ec.clear();
    }
}

void Arcade::start(int index) {
    if (index < 0 || index >= (int)defs_.size()) return;
    const GameDef& d = defs_[index];
    if      (d.engine == "snake")  game_ = std::make_unique<Snake>(d);
    else if (d.engine == "runner") game_ = std::make_unique<Runner>(d);
    else if (d.engine == "stardust") game_ = std::make_unique<Stardust>(d);
    else                           game_ = std::make_unique<Invaders>(d);
    game_->reset();
    current_ = index;
    paused_ = was_over_ = false;
    ready_ = 1.2f;
    if (best_.empty()) loadBest();
}

void Arcade::stop() {
    game_.reset();
    current_ = -1;
}

void Arcade::frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float energy, float bass,
                   const float* bars, int nbars, double time) {
    if (!game_) return;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);

    // Beats: the bass jumping well above its recent average
    GameAudio a;
    a.energy = energy; a.bass = bass; a.bars = bars; a.nbars = nbars;
    since_beat_ += dt;
    if (bass > 0.12f && bass > bass_avg_ * 1.35f && since_beat_ > 0.28f) { a.beat = true; since_beat_ = 0.f; }
    bass_avg_ += (bass - bass_avg_) * std::min(1.f, dt * 3.f);

    if (ImGui::IsKeyPressed(ImGuiKey_P, false) && !game_->over) paused_ = !paused_;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false) || (game_->over && ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
        game_->reset(); paused_ = was_over_ = false; ready_ = 1.2f;
    }
    if (ready_ > 0.f) ready_ -= dt;
    else if (!paused_ && !game_->over) game_->update(dt, a);

    // New best score, saved when the game ends
    const std::string& name = game_->def.name;
    if (game_->over && !was_over_) {
        was_over_ = true;
        if (game_->score > best_[name]) { best_[name] = game_->score; saveBest(); }
    }

    dl->PushClipRect(p0, p1, true);
    game_->draw(dl, p0, p1, a, time);

    // HUD
    const float fs = std::clamp((p1.y - p0.y) * 0.035f, 15.f, 30.f);
    const float pad = fs * 0.6f;
    char buf[64];
    hudText(dl, {p0.x + pad, p0.y + pad * 0.5f}, fs, IM_COL32_WHITE, name, game_->def.c1);
    std::snprintf(buf, sizeof(buf), "SCORE %d   BEST %d", game_->score, std::max(best_[name], game_->score));
    const ImVec2 sz = hudSize(fs, buf);
    hudText(dl, {p1.x - pad - sz.x, p0.y + pad * 0.5f}, fs, IM_COL32_WHITE, buf, game_->def.c2);
    const std::string help = std::string(game_->help()) + "    P: pause    R: restart    Esc: quit";
    const float hs = fs * 0.6f;
    const ImVec2 hz = hudSize(hs, help);
    hudText(dl, {(p0.x + p1.x - hz.x) * 0.5f, p1.y - hs - pad * 0.4f}, hs, IM_COL32(200, 200, 215, 170), help);

    if (ready_ > 0.f && !paused_) {
        const float bs = fs * 2.6f;
        const ImVec2 bz = hudSize(bs, "READY");
        hudText(dl, {(p0.x + p1.x - bz.x) * 0.5f, (p0.y + p1.y) * 0.5f - bz.y}, bs, IM_COL32_WHITE, "READY", game_->def.c1);
    }
    if (paused_ || game_->over) {
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 120));
        const std::string big = game_->over ? "GAME OVER" : "PAUSED";
        const float bs = fs * 2.6f;
        const ImVec2 bz = hudSize(bs, big);
        const ImVec2 c = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
        hudText(dl, {c.x - bz.x * 0.5f, c.y - bz.y}, bs, IM_COL32_WHITE, big, game_->over ? game_->def.c2 : game_->def.c1);
        const std::string sub = game_->over ? "Space or R: play again    Esc: quit" : "P: continue";
        const ImVec2 sub_sz = hudSize(fs, sub);
        hudText(dl, {c.x - sub_sz.x * 0.5f, c.y + fs * 0.4f}, fs, IM_COL32(220, 220, 235, 255), sub);
    }
    dl->PopClipRect();
}

// Best scores: <appdata>/games.ini, "score<TAB>name" per line
void Arcade::loadBest() {
    std::ifstream f(appDataPath("games.ini"));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        best_[line.substr(tab + 1)] = std::atoi(line.substr(0, tab).c_str());
    }
}

void Arcade::saveBest() const {
    std::ofstream f(appDataPath("games.ini"));
    for (auto& [name, score] : best_) if (score > 0) f << score << "\t" << name << "\n";
}
