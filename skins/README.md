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
only, no images) and `NeonCapsule` (shape from a PNG) for complete examples.

```jsonc
{
  "name": "My Skin",
  "author": "You",
  "width": 360, "height": 200,     // skin pixels (optional with a background image)
  "background": "bg.png",           // optional image
  "background_color": "#101018",    // used when there is no background image
  "shape": "rounded",               // rect | rounded | circle | image (default with a
                                    // background image: its transparency is the shape)
  "corner_radius": 24,              // for "rounded"
  "accent": "#00ff8c",              // default slider fill / visualizer colour
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
| `visualizer` | a visualizer inside the skin (click to change mode) | `mode`: `current`, `spectrum`, `oscilloscope`, `radial`, `bpm`, `particles`, `milkdrop` |
| `image` | a static picture | `image` |

**Button look.** A button draws its `image` (plus `image_hover`,
`image_pressed`, `image_on`) if given. Otherwise it draws a shape
(`shape`: rect / rounded / circle, `radius`) filled with `background` (plus
`background_hover`, `background_pressed`, `background_on`) and a vector `icon`
or a `text` label in `color` (plus `color_hover`, `color_pressed`, `color_on`).
With no icon or text, the icon is chosen from the action.

**Actions:** `play`, `pause`, `playpause`, `stop`, `next`, `prev`, `open`,
`menu`, `close`, `minimize`, `fullwindow` (leave classic mode), `playlist`,
`settings`, `fullscreen`, `mute`, `shuffle`, `repeat`, `viz_next`,
`preset_next`, `preset_prev` (MilkDrop).

**Icons:** `play`, `pause`, `stop`, `next`, `prev`, `open`, `close`,
`minimize`, `menu`, `fullwindow`, `fullscreen`, `volume`, `mute`, `shuffle`,
`repeat`, `viz`.

**Text content:** `title`, `time`, `remaining`, `duration`, `time_total`
(`1:23 / 4:56`), `bpm`, `volume`, `preset` (MilkDrop preset name), or `custom`
with `text`.
