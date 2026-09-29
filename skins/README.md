# PulseAmp skins

Skins are used in **Classic mode** (View → Classic Mode, or `Ctrl+M`): the player
becomes a compact, borderless window in the skin's own shape. Drag it by any
part of the skin that isn't a control. Right-click (or the skin's menu button)
for skins, zoom, always-on-top and "Back to full window". `Esc` also returns to
the full window.

PulseAmp looks for skins in:

- `skins/` next to `PulseAmp.exe` (the skins that ship with PulseAmp)
- `%APPDATA%\PulseAmp\skins` (Windows) or `~/.pulseamp/skins` (Linux/macOS) for your own

A skin can be a folder, a `.zip`, a `.wsz` or a `.paskin` (a zip with a
different name). Two kinds are supported.

## Winamp 2.x classic skins (`.wsz`)

Drop any classic Winamp skin in the skins folder. Tens of thousands are
archived at <https://skins.webamp.org/>. PulseAmp uses `main.bmp`,
`titlebar.bmp`, `cbuttons.bmp`, `numbers.bmp` / `nums_ex.bmp`, `playpaus.bmp`,
`text.bmp`, `posbar.bmp`, `volume.bmp`, `balance.bmp`, `monoster.bmp`,
`shufrep.bmp`, `viscolor.txt` (visualizer colours) and `region.txt` (the
window shape). The EQ and PL buttons open Settings and the Playlist in the full
window; the "shade" title-bar button returns to the full window.

## PulseAmp skins (`skin.json`)

A folder (or zip) with a `skin.json` and any images it uses (PNG, BMP or JPEG).
`//` and `/* */` comments are allowed in `skin.json`. See `PulseOrb` (shapes
only, no images), `NeonCapsule` (shape from a PNG) and `MidnightNeon` (a full
player with pages, lists, cover art and an equalizer) for complete examples.

```jsonc
{
  "name": "My Skin",
  "author": "You",
  "width": 360, "height": 200,     // skin pixels (optional with a background image)
  "background": "bg.png",           // optional image (PNG, BMP, JPEG or WebP)
  "background_color": "#101018",    // used when there is no background image
  "shape": "rounded",               // rect | rounded | circle | image (default with a
                                    // background image: its transparency is the shape)
  "corner_radius": 24,              // for "rounded"
  "accent": "#00ff8c",              // default slider fill / visualizer colour
  "start_page": "now",              // first page shown (see Pages below)
  "zoom": 1,                        // zoom to open the skin at (optional)
  "controls": [ ... ]
}
```

Colours are `#rrggbb` or `#rrggbbaa`. Every control has a `type` and a
`rect: [x, y, width, height]` in skin pixels.

| type | what it is | main properties |
|---|---|---|
| `button` | clickable | `action`, `icon` or `text`, or images |
| `toggle` | button with an on state (e.g. play/pause, shuffle) | as button, plus `color_on`, `background_on`, `image_on` |
| `slider` | seek or volume bar | `action`: `seek` / `volume`, `fill`, `background`, `color` (thumb), `radius`, `vertical`, `image` (track), `image_on` (thumb) |
| `text` | live text | `content`, `font_size`, `color`, `align`: left/center/right, `scroll`: true for a marquee |
| `visualizer` | a visualizer inside the skin (click to change mode). While a video plays it shows the video instead (click to pause) | `mode`: `current`, `spectrum`, `oscilloscope`, `radial`, `bpm`, `particles`, `neon`, `milkdrop`; `video`: false to always show the visualizer |
| `image` | a static picture | `image` |
| `panel` | a rounded card behind other controls | `background`, `border`, `radius`, `glow` |
| `art` | cover art of the current track (a neon placeholder when there is none) | `radius`, `glow` |
| `list` | a scrolling track list with thumbnails | `source`, `row_height`, `font_size`, `color`, `color2` (artist / duration), `color_on`, `background_on`, `border_on`, `fill` |
| `search` | a search box | `hint` (placeholder), `background`, `border`, `color`, `color2`, `radius` |
| `eq` | the 10-band equalizer sliders (drag; double-click resets a band) | `font_size`, `color2` (labels), `radius` (knob size) |
| `switch` | an on/off switch | `action` (e.g. `eq_toggle`), `fill` (on), `background` (off), `color` (knob) |
| `combo` | the equalizer preset dropdown | `background`, `border`, `color`, `fill`, `radius` |

**Button look.** A button draws its `image` (plus `image_hover`,
`image_pressed`, `image_on`) if given. Otherwise it draws a shape
(`shape`: rect / rounded / circle, `radius`) filled with `background` (plus
`background_hover`, `background_pressed`, `background_on`) and a vector `icon`
or a `text` label in `color` (plus `color_hover`, `color_pressed`, `color_on`).
With no icon or text, the icon is chosen from the action. A toggle with
`underline: true` underlines its text in `fill` when on (tab style).

**Neon look.** Any control can have a `border` (plus `border_on`,
`border_width`) and a `glow` colour with a `glow_size`. Text can switch colour
mid-string with `{#rrggbb}` markup: `"Pulse{#ff3cc8}Amp"`.

**Pages.** Give a control `"pages": ["now", "library"]` and it is only shown on
those pages; controls without `pages` are always shown. A button with the
action `page:<name>` switches page (and shows as "on" while it's active).

**Lists.** `source` is `playlist`, `library` (music found in your Music
folder), `online` (search results), `favorites`, or `left`: a list that shows
whichever of these was last picked with a `list:<source>` button. Double-click
plays a row; right-click for more. The `search` box filters the playlist,
library and favorites lists as you type; Enter searches YouTube when an online list or page is showing.

**Actions:** `play`, `pause`, `playpause`, `stop`, `next`, `prev`, `open`,
`add` (same as open), `menu`, `more` (same as menu), `close`, `minimize`,
`maximize`, `fullwindow` (leave classic mode), `playlist`, `settings`,
`fullscreen`, `mute`, `shuffle`, `repeat`, `favorite` (current track),
`eq_toggle`, `viz_next`, `preset_next`, `preset_prev` (MilkDrop),
`page:<name>`, `list:<source>`.

**Icons:** `play`, `pause`, `stop`, `next`, `prev`, `open`, `close`,
`minimize`, `maximize`, `menu`, `more`, `plus`, `fullwindow`, `fullscreen`,
`volume`, `mute`, `shuffle`, `repeat`, `viz`, `heart` (filled when on),
`gear`, `search`, `pulse`, `eqbars`, and `logo` (the PulseAmp logo, in its own colours).

**Text content:** `title`, `artist`, `album`, `format` (`FLAC | 44.1 kHz |
16 bit | 1024 kbps`), `page`, `time`, `remaining`, `duration`, `time_total`
(`1:23 / 4:56`), `bpm`, `volume`, `preset` (MilkDrop preset name), or `custom`
with `text`.
