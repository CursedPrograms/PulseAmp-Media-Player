[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>

# NovPlayer — Build Instructions

## What You Need

| Dependency | Version | Notes |
|---|---|---|
| CMake | ≥ 3.20 | [cmake.org](https://cmake.org) |
| C++ compiler | C++17 | GCC 10+, Clang 12+, MSVC 2022 |
| FFmpeg dev libs | 6.x / 7.x | avformat, avcodec, avutil, swscale, swresample, avfilter |
| SDL2 dev libs | ≥ 2.26 | [libsdl.org](https://libsdl.org) |
| OpenGL | 3.3+ | Provided by your GPU driver |
| ImGui | v1.90 | **Auto-downloaded** by CMake FetchContent |

---

## Linux (Ubuntu / Debian)

### 1. Install dependencies
```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libavfilter-dev \
    libsdl2-dev libgl1-mesa-dev libglu1-mesa-dev \
    zenity           # optional: file-open dialog
```

### 2. Clone & build
```bash
git clone https://github.com/yourname/novplayer.git
cd novplayer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 3. Run
```bash
./build/NovPlayer /path/to/movie.mkv
```

---

## Windows (Visual Studio 2022 + vcpkg)

### 1. Install vcpkg
```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg integrate install
```

### 2. Install dependencies via vcpkg
```powershell
C:\vcpkg\vcpkg install ffmpeg[avcodec,avformat,avutil,swscale,swresample,avfilter]:x64-windows
C:\vcpkg\vcpkg install sdl2:x64-windows
```

### 3. Configure & build
```powershell
git clone https://github.com/yourname/novplayer.git
cd novplayer
cmake -B build `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DNOV_BUNDLE_FFMPEG=ON `
  -DFFMPEG_BIN_DIR="C:\vcpkg\installed\x64-windows\bin"
cmake --build build --config Release
```

### 4. Run
```
build\Release\NovPlayer.exe C:\Videos\movie.mp4
```

### 5. Build installer (optional)
```powershell
# Install NSIS from https://nsis.sourceforge.io/
cmake -B build -DNOV_BUILD_INSTALLER=ON [... same flags ...]
cmake --build build --target installer
# → installer/NovPlayer-1.0.0-Setup.exe
```

---

## macOS (Homebrew)

### 1. Install dependencies
```bash
brew install cmake ffmpeg sdl2
```

### 2. Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

### 3. Run
```bash
./build/NovPlayer ~/Movies/movie.mp4
```

---

## MinGW-w64 on Windows (alternative to MSVC)

```bash
# Install MSYS2, then in MSYS2 MinGW64 shell:
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-SDL2

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `NOV_BUNDLE_FFMPEG` | `OFF` | Copy FFmpeg DLLs next to the exe (Windows) |
| `FFMPEG_BIN_DIR` | *(empty)* | Path to FFmpeg bin/ containing .dll files |
| `NOV_BUILD_INSTALLER` | `OFF` | Add `installer` CMake target (requires NSIS) |
| `CMAKE_BUILD_TYPE` | — | `Release` for optimized build, `Debug` for dev |

---

## Project Structure

```
NovPlayer/
├── CMakeLists.txt          ← Build system
├── BUILD.md                ← This file
├── src/
│   ├── main.cpp            ← Entry point, SDL2+GL window, main loop
│   ├── player.{h,cpp}      ← FFmpeg decode pipeline (demux + audio/video threads)
│   ├── audio_output.{h,cpp}← SDL2 audio device
│   ├── video_renderer.{h,cpp}← OpenGL RGBA texture upload
│   ├── visualizer.{h,cpp}  ← 5 visualizer modes with FFT
│   ├── theme_manager.{h,cpp}← 3 ImGui themes
│   ├── ui_manager.{h,cpp}  ← Full ImGui interface
│   ├── converter.{h,cpp}   ← FFmpeg format converter
│   ├── playlist.{h,cpp}    ← Playlist + smart resume
│   ├── bpm_detector.{h,cpp}← Real-time BPM detection
│   ├── spatial_audio.{h,cpp}← Stereo widener
│   └── waveform.{h,cpp}    ← Async full-file waveform generator
└── installer/
    └── setup.nsi           ← NSIS Windows installer script
```

---

## Supported Formats

| Container | Extensions |
|---|---|
| Video | `.mkv` `.mp4` `.avi` `.mov` `.webm` |
| Audio | `.mp3` `.flac` `.wav` `.ogg` `.aac` `.opus` `.m4a` |

Codec support depends on your installed FFmpeg build.  
The bundled FFmpeg from vcpkg/apt/brew covers H.264, H.265/HEVC, VP8, VP9, AV1, MP3, AAC, Vorbis, FLAC, and more.

---

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `Space` | Play / Pause |
| `←` / `→` | Seek ±5 seconds |
| `↑` / `↓` | Volume ±5% |
| `M` | Toggle mute |
| `N` / `P` | Next / Previous track |
| `Tab` | Toggle sidebar |
| `V` | Toggle visualizer |
| `F` | Toggle fullscreen |
| `Esc` | Quit |

---

## Themes

| Theme | Palette | Inspired by |
|---|---|---|
| **NeonAmp** | Black + Electric Green | Winamp 2.x |
| **ChromePlayer** | Silver-Grey + Steel Blue | Windows Media Player 9 |
| **MidnightFusion** | Deep Purple + Molten Gold | NovPlayer original |

---

## Visualizer Modes

| Mode | Description | Unique |
|---|---|---|
| Spectrum Bars | Classic FFT bar graph with peak hold | — |
| Oscilloscope | Real-time waveform trace | — |
| **Radial Spectrum** | Rotating circular FFT ring | ✅ NovPlayer exclusive |
| **BPM Pulse** | Beat-reactive expanding rings + spectrum border | ✅ NovPlayer exclusive |
| **Particle Storm** | Frequency-driven particle field | ✅ NovPlayer exclusive |

---

## Unique Features (not in other players)

1. **BPM Detection** — Real-time energy-based beat tracking; BPM shown in menu bar  
   and used to drive visualizer timing.

2. **Waveform Scrubber** — The seek bar displays the full-file RMS waveform  
   generated asynchronously on file open.  Seek by clicking anywhere on the waveform.

3. **Smart Resume** — Every file's playback position is saved to  
   `~/.novplayer/resume.dat` (Linux/macOS) or `%APPDATA%\NovPlayer\resume.dat` (Windows).  
   Next time you open the same file, it picks up exactly where you left off.

4. **Mood-Adaptive Color** — Optional mode that analyzes the live frequency balance  
   (bass/mid/treble ratio) and continuously shifts the visualizer accent color  
   to reflect the "mood" of the music.

5. **Spatial Audio / Stereo Widener** — Haas-effect cross-feed filter that expands  
   the stereo field for headphone listening.  Width is adjustable from 0× (mono) to 2× (extreme).

6. **Radial Spectrum + BPM Pulse + Particle Storm visualizers** —  
   Three completely original visualization modes found nowhere else.

---

## Troubleshooting

**"FFmpeg not found"** → Make sure the dev libraries are installed and CMake's  
`PKG_CONFIG_PATH` or vcpkg toolchain is configured.

**No audio on Linux** → Install `libasound2-dev` (ALSA) or `libpulse-dev` (PulseAudio)  
and rebuild SDL2 with audio support.

**Black video / no video decode** → Your FFmpeg build may lack the codec.  
Use a full build: `ffmpeg -version` should list H.264 and H.265 decoders.

**ImGui font warnings** → The app falls back to the built-in pixel font automatically;  
this is harmless.


---

<div align="center">
  <img src="/images/demo/KIDA002.jpg" alt="KIDA Robot" width="600"/>
</div>
<br>
<div align="center">© Cursed Entertainment 2026</div>
<br>
<div align="center">
  <a href="https://cursed-entertainment.itch.io/" target="_blank">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png" alt="CursedEntertainment Logo" style="width:250px;">
  </a>
</div>
<br>
<div align="center">
  <a href="https://github.com/SynthWomb" target="_blank">
    <img src="https://github.com/SynthWomb/synth.womb/blob/main/logos/synthwomb07.png" alt="SynthWomb" style="width:200px;"/>
  </a>
</div>