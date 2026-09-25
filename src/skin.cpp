// ─── skin.cpp ─────────────────────────────────────────────────────────────────
#include "skin.h"
#include "json.h"
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "miniz.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace fs = std::filesystem;

// ─── Files of a skin, from a folder or a zip (names matched case-insensitively) ─
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

struct SkinFiles {
    std::map<std::string, std::vector<uint8_t>> by_name;   // lower-case file name (no folder)

    const std::vector<uint8_t>* get(const std::string& name) const {
        auto it = by_name.find(lower(fs::path(name).filename().string()));
        return it == by_name.end() ? nullptr : &it->second;
    }
    std::string text(const std::string& name) const {
        auto* d = get(name);
        return d ? std::string(d->begin(), d->end()) : std::string();
    }
};

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool loadFiles(const std::string& path, SkinFiles& files, std::string& err) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    if (fs::is_directory(p, ec)) {
        for (auto& e : fs::recursive_directory_iterator(p, ec)) {
            if (!e.is_regular_file() || e.file_size(ec) > 32u * 1024 * 1024) continue;
            std::vector<uint8_t> data;
            if (readFile(e.path(), data))
                files.by_name.emplace(lower(e.path().filename().string()), std::move(data));
        }
        return true;
    }
    if (lower(p.filename().string()) == "skin.json")
        return loadFiles(p.parent_path().u8string(), files, err);

    // Zip (.wsz / .zip / .paskin)
    std::vector<uint8_t> zipdata;
    if (!readFile(p, zipdata)) { err = "Can't read " + path; return false; }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipdata.data(), zipdata.size(), 0)) {
        err = "Not a skin file (expected a .wsz/.zip archive)";
        return false;
    }
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        if (st.m_uncomp_size > 32u * 1024 * 1024) continue;
        size_t size = 0;
        void* mem = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!mem) continue;
        std::string name = lower(fs::u8path(st.m_filename).filename().u8string());
        const uint8_t* b = (const uint8_t*)mem;
        files.by_name.emplace(name, std::vector<uint8_t>(b, b + size));
        mz_free(mem);
    }
    mz_zip_reader_end(&zip);
    return true;
}

// ─── Image decoding (BMP / PNG / JPEG) via FFmpeg ─────────────────────────────
bool decodeImage(const std::vector<uint8_t>& data, SkinImage& out) {
    if (data.size() < 8) return false;
    AVCodecID id;
    if (data[0] == 'B' && data[1] == 'M')                     id = AV_CODEC_ID_BMP;
    else if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N') id = AV_CODEC_ID_PNG;
    else if (data[0] == 0xFF && data[1] == 0xD8)              id = AV_CODEC_ID_MJPEG;
    else return false;

    const AVCodec* codec = avcodec_find_decoder(id);
    if (!codec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool ok = false;
    // FFmpeg decoders may read a little past the end: pad the buffer
    std::vector<uint8_t> padded(data);
    padded.resize(data.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    pkt->data = padded.data();
    pkt->size = (int)data.size();
    if (avcodec_open2(ctx, codec, nullptr) >= 0 &&
        avcodec_send_packet(ctx, pkt) >= 0 && avcodec_receive_frame(ctx, frame) >= 0 &&
        frame->width > 0 && frame->height > 0) {
        SwsContext* sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         frame->width, frame->height, AV_PIX_FMT_RGBA,
                                         SWS_POINT, nullptr, nullptr, nullptr);
        if (sws) {
            out.w = frame->width;
            out.h = frame->height;
            out.rgba.assign((size_t)out.w * out.h * 4, 0);
            uint8_t* dst[4] = { out.rgba.data(), nullptr, nullptr, nullptr };
            int ls[4] = { out.w * 4, 0, 0, 0 };
            sws_scale(sws, frame->data, frame->linesize, 0, out.h, dst, ls);
            sws_freeContext(sws);
            ok = true;
        }
    }
    pkt->data = nullptr; pkt->size = 0;
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ok;
}

// Fill polygons into a mask (even-odd per polygon, union across polygons)
void fillPolygons(const std::vector<std::vector<std::pair<int,int>>>& polys,
                  int w, int h, std::vector<uint8_t>& mask) {
    for (auto& poly : polys) {
        if (poly.size() < 3) continue;
        for (int y = 0; y < h; ++y) {
            const float py = y + 0.5f;
            std::vector<float> xs;
            for (size_t i = 0; i < poly.size(); ++i) {
                auto [x0, y0] = poly[i];
                auto [x1, y1] = poly[(i + 1) % poly.size()];
                if ((y0 <= py && y1 > py) || (y1 <= py && y0 > py))
                    xs.push_back(x0 + (py - y0) * (float)(x1 - x0) / (float)(y1 - y0));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                int a = std::max(0, (int)std::ceil(xs[k] - 0.5f));
                int b = std::min(w, (int)std::ceil(xs[k + 1] - 0.5f));
                for (int x = a; x < b; ++x) mask[(size_t)y * w + x] = 1;
            }
        }
    }
}

// region.txt: [Normal] NumPoints=4,4  PointList=x,y, x,y, ...
bool parseRegion(const std::string& text, int w, int h, std::vector<uint8_t>& mask) {
    std::istringstream in(text);
    std::string line, section;
    std::vector<int> counts, points;
    auto numbers = [](const std::string& s) {
        std::vector<int> v;
        std::string cur;
        for (char c : s + ",") {
            if (std::isdigit((unsigned char)c) || c == '-') cur += c;
            else if (!cur.empty()) { v.push_back(std::atoi(cur.c_str())); cur.clear(); }
        }
        return v;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string l = lower(line);
        if (!l.empty() && l[0] == '[') { section = l; continue; }
        if (section != "[normal]") continue;
        auto eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string key = l.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        if (key == "numpoints") counts = numbers(l.substr(eq + 1));
        if (key == "pointlist") points = numbers(l.substr(eq + 1));
    }
    if (counts.empty() || points.empty()) return false;
    std::vector<std::vector<std::pair<int,int>>> polys;
    size_t p = 0;
    for (int n : counts) {
        std::vector<std::pair<int,int>> poly;
        for (int i = 0; i < n && p + 1 < points.size(); ++i, p += 2)
            poly.push_back({ points[p], points[p + 1] });
        polys.push_back(std::move(poly));
    }
    mask.assign((size_t)w * h, 0);
    fillPolygons(polys, w, h, mask);
    return true;
}

// viscolor.txt: 24 lines "r,g,b, // comment"
void parseVisColors(const std::string& text, ImU32 out[24]) {
    static const ImU32 defaults[24] = {   // Winamp's built-in colours
        IM_COL32(0,0,0,255), IM_COL32(24,33,41,255),
        IM_COL32(239,49,16,255), IM_COL32(206,41,16,255), IM_COL32(214,90,0,255), IM_COL32(214,102,0,255),
        IM_COL32(214,115,0,255), IM_COL32(198,123,8,255), IM_COL32(222,165,24,255), IM_COL32(214,181,33,255),
        IM_COL32(189,222,41,255), IM_COL32(148,222,33,255), IM_COL32(41,206,16,255), IM_COL32(50,190,16,255),
        IM_COL32(57,181,16,255), IM_COL32(49,156,8,255), IM_COL32(41,148,0,255), IM_COL32(24,132,8,255),
        IM_COL32(255,255,255,255), IM_COL32(214,214,222,255), IM_COL32(181,189,189,255), IM_COL32(160,170,175,255),
        IM_COL32(148,156,165,255), IM_COL32(150,150,150,255) };
    std::copy(defaults, defaults + 24, out);
    std::istringstream in(text);
    std::string line;
    int i = 0;
    while (i < 24 && std::getline(in, line)) {
        int r, g, b;
        if (std::sscanf(line.c_str(), " %d , %d , %d", &r, &g, &b) == 3)
            out[i++] = IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 255);
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
ImU32 Skin::parseColor(const std::string& s, ImU32 def) {
    unsigned r, g, b, a = 255;
    const char* c = s.c_str();
    if (*c == '#') ++c;
    size_t n = std::strlen(c);
    if (n == 8 && std::sscanf(c, "%2x%2x%2x%2x", &r, &g, &b, &a) == 4) return IM_COL32(r, g, b, a);
    if (n == 6 && std::sscanf(c, "%2x%2x%2x", &r, &g, &b) == 3)         return IM_COL32(r, g, b, 255);
    return def;
}

const SkinImage* Skin::image(const std::string& key) const {
    auto it = images.find(lower(key));
    return (it == images.end() || it->second.w == 0) ? nullptr : &it->second;
}

Skin::~Skin() { freeTextures(); }

void Skin::uploadTextures() {
    for (auto& [k, img] : images) {
        if (img.tex || img.rgba.empty()) continue;
        glGenTextures(1, &img.tex);
        glBindTexture(GL_TEXTURE_2D, img.tex);
        // Pixel art: keep it crisp when zoomed
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Skin::freeTextures() {
    for (auto& [k, img] : images)
        if (img.tex) { glDeleteTextures(1, &img.tex); img.tex = 0; }
}

// ─── Loading ──────────────────────────────────────────────────────────────────
std::unique_ptr<Skin> Skin::load(const std::string& path, std::string& err) {
    SkinFiles files;
    if (!loadFiles(path, files, err)) return nullptr;

    auto skin = std::make_unique<Skin>();
    skin->path = path;
    skin->name = fs::u8path(path).stem().u8string();

    auto loadImage = [&](const std::string& file, const std::string& key) {
        if (auto* d = files.get(file)) {
            SkinImage img;
            if (decodeImage(*d, img)) { skin->images[lower(key)] = std::move(img); return true; }
        }
        return false;
    };

    // ── PulseAmp skin ─────────────────────────────────────────────────────────
    if (files.get("skin.json")) {
        std::string jerr;
        Json j = Json::parse(files.text("skin.json"), jerr);
        if (!j.isObject()) { err = "skin.json: " + (jerr.empty() ? "not an object" : jerr); return nullptr; }
        skin->kind   = Kind::PulseAmp;
        skin->name   = j["name"].str(skin->name);
        skin->author = j["author"].str();
        skin->width  = (int)j["width"].num(0);
        skin->height = (int)j["height"].num(0);
        skin->bg_color = parseColor(j["background_color"].str(), skin->bg_color);
        skin->accent   = parseColor(j["accent"].str(), skin->accent);
        skin->shape    = lower(j["shape"].str("rect"));
        skin->corner_radius = (float)j["corner_radius"].num(16);

        // Every image a control or the background references
        auto want = [&](const std::string& f) { if (!f.empty() && !skin->image(f)) loadImage(f, f); };
        skin->background = j["background"].str();
        want(skin->background);
        if (const SkinImage* bg = skin->image(skin->background)) {
            if (skin->width  <= 0) skin->width  = bg->w;
            if (skin->height <= 0) skin->height = bg->h;
            if (!j.has("shape")) skin->shape = "image";   // shape from the image's alpha
        }
        if (skin->width <= 0 || skin->height <= 0 || skin->width > 4096 || skin->height > 4096) {
            err = "skin.json: set width and height (or a background image)";
            return nullptr;
        }

        for (auto& c : j["controls"].items()) {
            SkinControl sc;
            sc.type     = lower(c["type"].str("button"));
            sc.action   = lower(c["action"].str());
            sc.content  = lower(c["content"].str());
            sc.text     = c["text"].str();
            sc.icon     = lower(c["icon"].str());
            const Json& r = c["rect"];
            sc.x = (float)r[0].num(); sc.y = (float)r[1].num();
            sc.w = (float)r[2].num(); sc.h = (float)r[3].num();
            sc.image = c["image"].str();          want(sc.image);
            sc.image_hover = c["image_hover"].str();   want(sc.image_hover);
            sc.image_pressed = c["image_pressed"].str(); want(sc.image_pressed);
            sc.image_on = c["image_on"].str();    want(sc.image_on);
            sc.color         = parseColor(c["color"].str(), sc.color);
            sc.color_hover   = parseColor(c["color_hover"].str(), 0);
            sc.color_pressed = parseColor(c["color_pressed"].str(), 0);
            sc.color_on      = parseColor(c["color_on"].str(), 0);
            sc.bg            = parseColor(c["background"].str(), 0);
            sc.bg_hover      = parseColor(c["background_hover"].str(), 0);
            sc.bg_pressed    = parseColor(c["background_pressed"].str(), 0);
            sc.bg_on         = parseColor(c["background_on"].str(), 0);
            sc.fill          = parseColor(c["fill"].str(), skin->accent);
            sc.shape     = lower(c["shape"].str("rounded"));
            sc.radius    = (float)c["radius"].num(6);
            sc.font_size = (float)c["font_size"].num(14);
            sc.align     = lower(c["align"].str("left"));
            sc.scroll    = c["scroll"].boolean(false);
            sc.vertical  = c["vertical"].boolean(false);
            sc.viz_mode  = lower(c["mode"].str("current"));
            skin->controls.push_back(sc);
        }

        // Window shape
        const int w = skin->width, h = skin->height;
        skin->mask.assign((size_t)w * h, 1);
        if (skin->shape == "image") {
            if (const SkinImage* bg = skin->image(skin->background)) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        bool in = x < bg->w && y < bg->h && bg->rgba[((size_t)y * bg->w + x) * 4 + 3] > 24;
                        skin->mask[(size_t)y * w + x] = in;
                    }
            }
        } else if (skin->shape == "circle" || skin->shape == "ellipse") {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    float dx = (x + 0.5f - w * 0.5f) / (w * 0.5f), dy = (y + 0.5f - h * 0.5f) / (h * 0.5f);
                    skin->mask[(size_t)y * w + x] = dx * dx + dy * dy <= 1.f;
                }
        } else if (skin->shape == "rounded") {
            const float r = std::min({ skin->corner_radius, w * 0.5f, h * 0.5f });
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    float cx = std::clamp(x + 0.5f, r, w - r), cy = std::clamp(y + 0.5f, r, h - r);
                    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    skin->mask[(size_t)y * w + x] = dx * dx + dy * dy <= r * r;
                }
        }
        return skin;
    }

    // ── Winamp classic skin ──────────────────────────────────────────────────
    if (!files.get("main.bmp")) {
        err = "Not a skin: no main.bmp (Winamp) or skin.json (PulseAmp) found";
        return nullptr;
    }
    skin->kind = Kind::Winamp;
    skin->width = 275;
    skin->height = 116;
    static const char* sprites[] = { "main", "titlebar", "cbuttons", "playpaus", "text", "posbar",
                                     "volume", "balance", "monoster", "shufrep" };
    for (auto s : sprites) loadImage(std::string(s) + ".bmp", s);
    if (!loadImage("numbers.bmp", "numbers")) loadImage("nums_ex.bmp", "numbers");
    if (!skin->image("balance") && skin->image("volume"))
        skin->images["balance"] = skin->images["volume"];   // some skins omit balance.bmp
    if (!skin->image("main")) { err = "main.bmp could not be read"; return nullptr; }

    parseVisColors(files.text("viscolor.txt"), skin->viscolors);
    if (!parseRegion(files.text("region.txt"), skin->width, skin->height, skin->mask))
        skin->mask.assign((size_t)skin->width * skin->height, 1);
    return skin;
}
