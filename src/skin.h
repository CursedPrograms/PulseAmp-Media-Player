#pragma once
// ─── skin.h ───────────────────────────────────────────────────────────────────
// Skins for "Classic mode": a compact, borderless, shaped window.
//
//  • Winamp 2.x classic skins (.wsz / .zip / folder): main.bmp, cbuttons.bmp,
//    titlebar.bmp, ... with the window shape from region.txt.
//  • PulseAmp skins (.paskin / .zip / folder with skin.json): a background
//    image or shape plus freely placed controls - see skins/README.md.
// ─────────────────────────────────────────────────────────────────────────────
#include <imgui.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct SkinImage {
    std::vector<uint8_t> rgba;   // w*h*4
    int      w = 0, h = 0;
    unsigned tex = 0;            // GL texture, created by Skin::uploadTextures()
};

// One element of a PulseAmp skin (see skins/README.md for the format)
struct SkinControl {
    std::string type;            // button | toggle | slider | text | visualizer | image |
                                 // panel | list | art | search | eq | switch | combo
    std::string action;          // play, pause, playpause, stop, next, prev, open, menu,
                                 // close, minimize, fullwindow, mute, shuffle, repeat,
                                 // fullscreen, seek, volume, preset_next, preset_prev
    std::string content;         // text: title, time, remaining, duration, bpm, preset, custom
    std::string text;            // custom text / button label
    std::string icon;            // vector icon for buttons (play, pause, stop, next, ...)
    float x = 0, y = 0, w = 0, h = 0;
    std::string image, image_hover, image_pressed, image_on;   // image keys
    ImU32 color = IM_COL32(255, 255, 255, 255);  // icon / text / fill colour
    ImU32 color_hover = 0, color_pressed = 0, color_on = 0;
    ImU32 bg = 0, bg_hover = 0, bg_pressed = 0, bg_on = 0;     // button / track background
    ImU32 fill = 0;              // slider: filled part
    std::string shape = "rounded"; // rect | rounded | circle
    float radius = 6.f;          // rounded corners / slider thumb radius
    float font_size = 14.f;
    std::string align = "left";  // left | center | right
    bool  scroll = false;        // text: marquee when too long
    bool  vertical = false;      // slider direction
    std::string viz_mode;        // visualizer: current | spectrum | oscilloscope | radial | bpm | particles | neon | milkdrop
    bool  video = true;          // visualizer: show the video instead while one plays

    // Neon look
    ImU32 border = 0, border_on = 0;   // outline colour (off / on)
    float border_width = 1.5f;
    ImU32 glow = 0;                    // glow colour (0 = none)
    float glow_size = 10.f;
    bool  underline = false;           // toggle: underline when on (tab style)

    // Pages: shown only on these skin pages (empty = always)
    std::vector<std::string> pages;

    // list: playlist | library | online | favorites | left (follows list:* buttons)
    std::string source;
    float row_height = 60.f;
    ImU32 color2 = 0;                  // secondary text (artist, duration, labels)

    std::string hint;                  // search: placeholder text
};

// Decode a BMP / PNG / JPEG / WebP image from memory into RGBA
bool decodeImageData(const uint8_t* data, size_t size, SkinImage& out);

// swscale into a tightly packed dst_w x dst_h RGBA buffer, safely (swscale may
// write past a row's end: never give it an exact-size buffer)
struct SwsContext;
bool swsToRGBA(SwsContext* sws, const uint8_t* const src[], const int src_ls[], int src_h,
               int dst_w, int dst_h, std::vector<uint8_t>& out);

class Skin {
public:
    enum class Kind { Winamp, PulseAmp };

    // path: .wsz / .zip / .paskin file, a folder, or a skin.json file
    static std::unique_ptr<Skin> load(const std::string& path, std::string& error);
    ~Skin();

    Kind        kind = Kind::Winamp;
    std::string name, author, path;
    int         width = 0, height = 0;          // skin pixels
    std::vector<uint8_t> mask;                  // width*height, 1 = inside the window shape

    const SkinImage* image(const std::string& key) const;
    std::map<std::string, SkinImage> images;

    // Winamp: visualizer colours from viscolor.txt (0 bg, 1 dots, 2-17 bars top..bottom,
    // 18-22 oscilloscope, 23 peak dots)
    ImU32 viscolors[24] = {};

    // PulseAmp
    std::vector<SkinControl> controls;
    std::string background;                    // image key ("" = background colour/shape)
    ImU32       bg_color = IM_COL32(20, 20, 24, 255);
    std::string shape = "rect";                // rect | rounded | circle | image
    float       corner_radius = 16.f;
    ImU32       accent = IM_COL32(0, 255, 140, 255);
    std::string start_page = "now";           // first page shown
    float       default_zoom = 0.f;           // 0 = keep the current zoom

    void uploadTextures();   // GL context must be current
    void freeTextures();

    // Parse "#rrggbb" / "#rrggbbaa"
    static ImU32 parseColor(const std::string& s, ImU32 def);
};
