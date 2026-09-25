# ─── package-windows.ps1 ──────────────────────────────────────────────────────
# Builds the portable PulseAmp folder + zip (and optionally the installer)
# from a finished Windows build.
#
#   .\scripts\package-windows.ps1 -BuildDir build -FFmpegBin C:\ffmpeg\bin [-SDL2Dll path] [-Installer]
#
# Downloads (once, cached in <OutDir>\.cache): yt-dlp, the MilkDrop
# "Cream of the Crop" presets and the MilkDrop texture pack.
# ─────────────────────────────────────────────────────────────────────────────
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $FFmpegBin,      # folder with avcodec-*.dll etc.
    [string] $SDL2Dll = "",                           # default: SDL2.dll in the build dir
    [string] $OutDir = "dist",
    [switch] $Installer,                              # also build the NSIS installer
    [string] $MakeNsis = "makensis"
)
$ErrorActionPreference = "Stop"
$Root    = Split-Path $PSScriptRoot -Parent
$Version = (Select-String -Path "$Root\CMakeLists.txt" -Pattern 'project\(PulseAmp VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$Dist    = Join-Path $OutDir "PulseAmp"
$Cache   = Join-Path $OutDir ".cache"
New-Item -ItemType Directory -Force $Dist, $Cache, "$Dist\licenses" | Out-Null

function Fetch($url, $file) {
    $path = Join-Path $Cache $file
    if (-not (Test-Path $path)) {
        Write-Host "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
    }
    return $path
}
function Unzip($zip, $name) {
    $dir = Join-Path $Cache $name
    if (-not (Test-Path $dir)) { Expand-Archive -Path $zip -DestinationPath $dir -Force }
    return (Get-ChildItem $dir -Directory | Select-Object -First 1).FullName
}

# ── Program + DLLs ────────────────────────────────────────────────────────────
Copy-Item "$BuildDir\PulseAmp.exe" $Dist -Force
Get-ChildItem "$BuildDir\*.dll" | Copy-Item -Destination $Dist -Force      # projectM, GLEW, SDL2 (if bundled)
if ($SDL2Dll) { Copy-Item $SDL2Dll $Dist -Force }
if (-not (Test-Path "$Dist\SDL2.dll")) { throw "SDL2.dll not found: pass -SDL2Dll" }
foreach ($lib in "avcodec", "avformat", "avutil", "swresample", "swscale") {
    $dll = Get-ChildItem "$FFmpegBin\$lib-*.dll" | Select-Object -First 1
    if (-not $dll) { throw "$lib-*.dll not found in $FFmpegBin" }
    Copy-Item $dll.FullName $Dist -Force
}

# ── yt-dlp (YouTube / SoundCloud) ─────────────────────────────────────────────
Copy-Item (Fetch "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" "yt-dlp.exe") $Dist -Force

# ── MilkDrop presets + textures ───────────────────────────────────────────────
$presets = Unzip (Fetch "https://github.com/projectM-visualizer/presets-cream-of-the-crop/archive/refs/heads/master.zip" "presets.zip") "presets"
$textures = Unzip (Fetch "https://github.com/projectM-visualizer/presets-milkdrop-texture-pack/archive/refs/heads/master.zip" "textures.zip") "textures"
if (-not (Test-Path "$Dist\presets")) {
    New-Item -ItemType Directory "$Dist\presets" | Out-Null
    Get-ChildItem $presets -Directory | Copy-Item -Destination "$Dist\presets" -Recurse
}
if (-not (Test-Path "$Dist\textures")) { Copy-Item "$textures\textures" "$Dist\textures" -Recurse }

# ── Skins ─────────────────────────────────────────────────────────────────────
Copy-Item "$Root\skins" $Dist -Recurse -Force

# ── Licenses ──────────────────────────────────────────────────────────────────
Copy-Item "$Root\LICENSE" "$Dist\LICENSE.txt" -Force
$ffLicense = Join-Path (Split-Path $FFmpegBin -Parent) "LICENSE.txt"
if (Test-Path $ffLicense) { Copy-Item $ffLicense "$Dist\licenses\FFmpeg-LICENSE.txt" -Force }
Copy-Item "$presets\LICENSE.md" "$Dist\licenses\MilkDrop-presets-LICENSE.md" -Force
@"
PulseAmp $Version bundles:
  FFmpeg      (LGPL 2.1+)   https://ffmpeg.org          - licenses\FFmpeg-LICENSE.txt
  SDL2        (zlib)        https://libsdl.org
  projectM    (LGPL 2.1)    https://github.com/projectM-visualizer/projectm
  GLEW        (BSD/MIT)     https://github.com/nigels-com/glew
  yt-dlp      (Unlicense)   https://github.com/yt-dlp/yt-dlp
  MilkDrop "Cream of the Crop" presets - licenses\MilkDrop-presets-LICENSE.md
  Dear ImGui  (MIT), miniz (MIT)
"@ | Set-Content "$Dist\licenses\THIRD-PARTY.txt" -Encoding utf8

# ── Zips ──────────────────────────────────────────────────────────────────────
# GitHub rejects files over 100 MB, so the MilkDrop presets get their own zip:
#   PulseAmp-win64.zip                  the player (extract anywhere, run PulseAmp.exe)
#   PulseAmp-milkdrop-presets.zip       extract into the PulseAmp folder (adds presets\)
# Names carry no version so download links stay the same between releases.
$zip        = Join-Path $OutDir "PulseAmp-win64.zip"
$presetsZip = Join-Path $OutDir "PulseAmp-milkdrop-presets.zip"
foreach ($z in $zip, $presetsZip) { if (Test-Path $z) { Remove-Item $z } }
$items = Get-ChildItem $Dist | Where-Object { $_.Name -ne "presets" }
$stage = Join-Path $Cache "zip-stage\PulseAmp"
if (Test-Path (Split-Path $stage)) { Remove-Item (Split-Path $stage) -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
$items | Copy-Item -Destination $stage -Recurse
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Push-Location $Dist
Compress-Archive -Path "presets" -DestinationPath (Join-Path (Get-Location).Path "..\$(Split-Path $presetsZip -Leaf)") -CompressionLevel Optimal
Pop-Location
Write-Host "Portable build: $Dist"
Write-Host "Zip:            $zip  ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
Write-Host "Presets zip:    $presetsZip  ($([math]::Round((Get-Item $presetsZip).Length / 1MB, 1)) MB)"

# ── Installer (NSIS) ──────────────────────────────────────────────────────────
if ($Installer) {
    $distAbs = (Resolve-Path $Dist).Path
    & $MakeNsis "/DPRODUCT_VERSION=$Version" "/DDIST_DIR=$distAbs" "$Root\installer\setup.nsi"
    if ($LASTEXITCODE -ne 0) { throw "makensis failed" }
    $setup = Join-Path $OutDir "PulseAmp-Setup.exe"
    Move-Item "$Root\installer\PulseAmp-$Version-Setup.exe" $setup -Force
    Write-Host "Installer:      $setup  ($([math]::Round((Get-Item $setup).Length / 1MB, 1)) MB)"
}
