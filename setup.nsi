; ─── installer/setup.nsi ───────────────────────────────────────────────────────
; NSIS installer for NovPlayer
; Build: makensis /DPRODUCT_VERSION=1.0.0 /DAPP_EXE=path\to\NovPlayer.exe setup.nsi
; ──────────────────────────────────────────────────────────────────────────────

!define PRODUCT_NAME    "NovPlayer"
!define PRODUCT_PUBLISHER "NovPlayer Project"
!define PRODUCT_URL     "https://github.com/yourname/novplayer"
!define INSTALL_DIR     "$PROGRAMFILES64\NovPlayer"
!define REG_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovPlayer"

; Bundled FFmpeg DLL directory – set at build time or adjust below
!ifndef FFMPEG_DLLS_DIR
  !define FFMPEG_DLLS_DIR "..\ffmpeg-win64\bin"
!endif

!ifndef APP_EXE
  !define APP_EXE "..\build\Release\NovPlayer.exe"
!endif

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.0.0"
!endif

; ── NSIS settings ─────────────────────────────────────────────────────────────
Name            "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile         "NovPlayer-${PRODUCT_VERSION}-Setup.exe"
InstallDir      "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor   /SOLID lzma
ShowInstDetails show

; ── Pages ─────────────────────────────────────────────────────────────────────
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

LicenseData "..\LICENSE.txt"

; ── Installer sections ────────────────────────────────────────────────────────
Section "NovPlayer (required)" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Main executable
  File "${APP_EXE}"

  ; SDL2 runtime
  File /oname=SDL2.dll "SDL2.dll"

  ; FFmpeg DLLs – the six required libraries
  File /oname=avcodec-61.dll   "${FFMPEG_DLLS_DIR}\avcodec-61.dll"
  File /oname=avformat-61.dll  "${FFMPEG_DLLS_DIR}\avformat-61.dll"
  File /oname=avutil-59.dll    "${FFMPEG_DLLS_DIR}\avutil-59.dll"
  File /oname=swscale-8.dll    "${FFMPEG_DLLS_DIR}\swscale-8.dll"
  File /oname=swresample-5.dll "${FFMPEG_DLLS_DIR}\swresample-5.dll"
  File /oname=avfilter-10.dll  "${FFMPEG_DLLS_DIR}\avfilter-10.dll"

  ; ── File associations ────────────────────────────────────────────────────────
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".mkv"  "MKV Video"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".mp4"  "MP4 Video"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".avi"  "AVI Video"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".mov"  "MOV Video"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".mp3"  "MP3 Audio"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".flac" "FLAC Audio"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".wav"  "WAV Audio"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".ogg"  "OGG Audio"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".aac"  "AAC Audio"
  ${RegisterExtension} "$INSTDIR\NovPlayer.exe" ".opus" "Opus Audio"

  ; ── Shortcuts ────────────────────────────────────────────────────────────────
  CreateDirectory "$SMPROGRAMS\NovPlayer"
  CreateShortcut  "$SMPROGRAMS\NovPlayer\NovPlayer.lnk" \
                  "$INSTDIR\NovPlayer.exe" "" "$INSTDIR\NovPlayer.exe" 0
  CreateShortcut  "$DESKTOP\NovPlayer.lnk" \
                  "$INSTDIR\NovPlayer.exe" "" "$INSTDIR\NovPlayer.exe" 0

  ; ── Registry ─────────────────────────────────────────────────────────────────
  WriteRegStr   HKLM "${REG_KEY}" "DisplayName"    "${PRODUCT_NAME}"
  WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr   HKLM "${REG_KEY}" "Publisher"      "${PRODUCT_PUBLISHER}"
  WriteRegStr   HKLM "${REG_KEY}" "URLInfoAbout"   "${PRODUCT_URL}"
  WriteRegStr   HKLM "${REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKLM "${REG_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKLM "${REG_KEY}" "NoModify"       1
  WriteRegDWORD HKLM "${REG_KEY}" "NoRepair"       1
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Optional: Visual C++ Redistributable check
Section "VC++ Runtime" SEC_VCRT
  ; Download and run vc_redist.x64.exe if not present
  IfFileExists "$SYSDIR\vcruntime140.dll" done
    ExecWait '"$INSTDIR\vc_redist.x64.exe" /install /quiet /norestart'
  done:
SectionEnd

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
  ; Remove files
  Delete "$INSTDIR\NovPlayer.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\avcodec-61.dll"
  Delete "$INSTDIR\avformat-61.dll"
  Delete "$INSTDIR\avutil-59.dll"
  Delete "$INSTDIR\swscale-8.dll"
  Delete "$INSTDIR\swresample-5.dll"
  Delete "$INSTDIR\avfilter-10.dll"
  Delete "$INSTDIR\uninstall.exe"
  RMDir  "$INSTDIR"

  ; Shortcuts
  Delete "$SMPROGRAMS\NovPlayer\NovPlayer.lnk"
  RMDir  "$SMPROGRAMS\NovPlayer"
  Delete "$DESKTOP\NovPlayer.lnk"

  ; File associations
  ${UnRegisterExtension} ".mkv"  "MKV Video"
  ${UnRegisterExtension} ".mp4"  "MP4 Video"
  ${UnRegisterExtension} ".avi"  "AVI Video"
  ${UnRegisterExtension} ".mp3"  "MP3 Audio"
  ${UnRegisterExtension} ".flac" "FLAC Audio"
  ${UnRegisterExtension} ".wav"  "WAV Audio"

  ; Registry
  DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
