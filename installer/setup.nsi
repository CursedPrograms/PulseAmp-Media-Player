; ─── installer/setup.nsi ───────────────────────────────────────────────────────
; NSIS installer for NovPlayer
; Normally built by CMake:  cmake --build build --target installer
; Manual build:
;   makensis /DPRODUCT_VERSION=1.0.0 /DAPP_EXE=path\to\NovPlayer.exe
;            /DSDL2_DLL=path\to\SDL2.dll /DFFMPEG_DLLS_DIR=path\to\ffmpeg\bin setup.nsi
; Paths below are relative to this folder (makensis runs from the script's directory).
; ──────────────────────────────────────────────────────────────────────────────

!define PRODUCT_NAME    "NovPlayer"
!define PRODUCT_PUBLISHER "Cursed Entertainment"
!define PRODUCT_URL     "https://github.com/CursedPrograms/media_player"
!define INSTALL_DIR     "$PROGRAMFILES64\NovPlayer"
!define REG_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovPlayer"
!define PROG_ID         "NovPlayer.Media"

; Bundled FFmpeg DLL directory – set at build time or adjust below
!ifndef FFMPEG_DLLS_DIR
  !define FFMPEG_DLLS_DIR "..\ffmpeg-win64\bin"
!endif

!ifndef SDL2_DLL
  !define SDL2_DLL "..\build\Release\SDL2.dll"
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

LicenseData "..\LICENSE"

; ── File associations ─────────────────────────────────────────────────────────
; Adds NovPlayer to each extension's "Open with" list without taking over the
; user's default app (Windows only lets the user change defaults).
!macro AddOpenWith EXT
  WriteRegStr HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}" ""
  WriteRegStr HKLM "Software\Classes\Applications\NovPlayer.exe\SupportedTypes" "${EXT}" ""
!macroend

!macro RemoveOpenWith EXT
  DeleteRegValue HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}"
!macroend

!macro ForEachMediaExt MACRO
  !insertmacro ${MACRO} ".mkv"
  !insertmacro ${MACRO} ".mp4"
  !insertmacro ${MACRO} ".avi"
  !insertmacro ${MACRO} ".mov"
  !insertmacro ${MACRO} ".webm"
  !insertmacro ${MACRO} ".mp3"
  !insertmacro ${MACRO} ".flac"
  !insertmacro ${MACRO} ".wav"
  !insertmacro ${MACRO} ".ogg"
  !insertmacro ${MACRO} ".aac"
  !insertmacro ${MACRO} ".opus"
  !insertmacro ${MACRO} ".m4a"
!macroend

; ── Installer sections ────────────────────────────────────────────────────────
Section "NovPlayer (required)" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetShellVarContext all

  ; Main executable
  File "${APP_EXE}"

  ; SDL2 runtime
  File "${SDL2_DLL}"

  ; FFmpeg DLLs (whatever version the app was built against)
  File "${FFMPEG_DLLS_DIR}\*.dll"

  ; ── File associations ────────────────────────────────────────────────────────
  WriteRegStr HKLM "Software\Classes\${PROG_ID}" "" "NovPlayer media file"
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\DefaultIcon" "" "$INSTDIR\NovPlayer.exe,0"
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\shell\open\command" "" '"$INSTDIR\NovPlayer.exe" "%1"'
  WriteRegStr HKLM "Software\Classes\Applications\NovPlayer.exe\shell\open\command" "" '"$INSTDIR\NovPlayer.exe" "%1"'
  !insertmacro ForEachMediaExt AddOpenWith
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)' ; SHCNE_ASSOCCHANGED

  ; ── Shortcuts ────────────────────────────────────────────────────────────────
  CreateDirectory "$SMPROGRAMS\NovPlayer"
  CreateShortcut  "$SMPROGRAMS\NovPlayer\NovPlayer.lnk" \
                  "$INSTDIR\NovPlayer.exe" "" "$INSTDIR\NovPlayer.exe" 0
  CreateShortcut  "$DESKTOP\NovPlayer.lnk" \
                  "$INSTDIR\NovPlayer.exe" "" "$INSTDIR\NovPlayer.exe" 0

  ; ── Registry ─────────────────────────────────────────────────────────────────
  WriteRegStr   HKLM "${REG_KEY}" "DisplayName"    "${PRODUCT_NAME}"
  WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr   HKLM "${REG_KEY}" "DisplayIcon"    "$INSTDIR\NovPlayer.exe"
  WriteRegStr   HKLM "${REG_KEY}" "Publisher"      "${PRODUCT_PUBLISHER}"
  WriteRegStr   HKLM "${REG_KEY}" "URLInfoAbout"   "${PRODUCT_URL}"
  WriteRegStr   HKLM "${REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKLM "${REG_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${REG_KEY}" "NoModify"       1
  WriteRegDWORD HKLM "${REG_KEY}" "NoRepair"       1
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Optional: Visual C++ runtime (only needed for MSVC builds; MinGW builds link
; their runtime statically). Pass /DVC_REDIST=path\to\vc_redist.x64.exe to include it.
!ifdef VC_REDIST
Section "VC++ Runtime" SEC_VCRT
  IfFileExists "$SYSDIR\vcruntime140.dll" done
    SetOutPath "$PLUGINSDIR"
    File "/oname=vc_redist.x64.exe" "${VC_REDIST}"
    ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart'
  done:
SectionEnd
!endif

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
  SetShellVarContext all

  ; Remove files
  Delete "$INSTDIR\NovPlayer.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\uninstall.exe"
  RMDir  "$INSTDIR"

  ; Shortcuts
  Delete "$SMPROGRAMS\NovPlayer\NovPlayer.lnk"
  RMDir  "$SMPROGRAMS\NovPlayer"
  Delete "$DESKTOP\NovPlayer.lnk"

  ; File associations
  !insertmacro ForEachMediaExt RemoveOpenWith
  DeleteRegKey HKLM "Software\Classes\${PROG_ID}"
  DeleteRegKey HKLM "Software\Classes\Applications\NovPlayer.exe"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  ; Registry
  DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
