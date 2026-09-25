; ─── installer/setup.nsi ───────────────────────────────────────────────────────
; NSIS installer for PulseAmp: installs the portable build folder.
; Normally built by  scripts\package-windows.ps1 -Installer  or manually:
;   makensis /DPRODUCT_VERSION=1.0.0 /DDIST_DIR=C:\path\to\dist\PulseAmp setup.nsi
; Paths below are relative to this folder (makensis runs from the script's directory).
; ──────────────────────────────────────────────────────────────────────────────

!define PRODUCT_NAME    "PulseAmp"
!define PRODUCT_PUBLISHER "Cursed Entertainment"
!define PRODUCT_URL     "https://github.com/CursedPrograms/media_player"
!define INSTALL_DIR     "$PROGRAMFILES64\PulseAmp"
!define REG_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\PulseAmp"
!define PROG_ID         "PulseAmp.Media"

; The portable build (PulseAmp.exe, DLLs, yt-dlp, presets, textures, skins)
!ifndef DIST_DIR
  !define DIST_DIR "..\dist\PulseAmp"
!endif

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.0.0"
!endif

; ── NSIS settings ─────────────────────────────────────────────────────────────
Name            "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile         "PulseAmp-${PRODUCT_VERSION}-Setup.exe"
InstallDir      "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor   /SOLID lzma
ShowInstDetails show

; Installer file properties (credits)
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName"     "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "Copyright (c) 2026 Cursed Entertainment. Created by Farica Kimora."
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey "FileVersion"     "${PRODUCT_VERSION}"

; ── Pages ─────────────────────────────────────────────────────────────────────
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

LicenseData "..\LICENSE"

; ── File associations ─────────────────────────────────────────────────────────
; Adds PulseAmp to each extension's "Open with" list without taking over the
; user's default app (Windows only lets the user change defaults).
!macro AddOpenWith EXT
  WriteRegStr HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROG_ID}" ""
  WriteRegStr HKLM "Software\Classes\Applications\PulseAmp.exe\SupportedTypes" "${EXT}" ""
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
Section "PulseAmp (required)" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetShellVarContext all

  ; Everything from the portable build
  File /r "${DIST_DIR}\*.*"

  ; ── File associations ────────────────────────────────────────────────────────
  WriteRegStr HKLM "Software\Classes\${PROG_ID}" "" "PulseAmp media file"
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\DefaultIcon" "" "$INSTDIR\PulseAmp.exe,0"
  WriteRegStr HKLM "Software\Classes\${PROG_ID}\shell\open\command" "" '"$INSTDIR\PulseAmp.exe" "%1"'
  WriteRegStr HKLM "Software\Classes\Applications\PulseAmp.exe\shell\open\command" "" '"$INSTDIR\PulseAmp.exe" "%1"'
  !insertmacro ForEachMediaExt AddOpenWith
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)' ; SHCNE_ASSOCCHANGED

  ; ── Shortcuts ────────────────────────────────────────────────────────────────
  CreateDirectory "$SMPROGRAMS\PulseAmp"
  CreateShortcut  "$SMPROGRAMS\PulseAmp\PulseAmp.lnk" \
                  "$INSTDIR\PulseAmp.exe" "" "$INSTDIR\PulseAmp.exe" 0
  CreateShortcut  "$DESKTOP\PulseAmp.lnk" \
                  "$INSTDIR\PulseAmp.exe" "" "$INSTDIR\PulseAmp.exe" 0

  ; ── Registry ─────────────────────────────────────────────────────────────────
  WriteRegStr   HKLM "${REG_KEY}" "DisplayName"    "${PRODUCT_NAME}"
  WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr   HKLM "${REG_KEY}" "DisplayIcon"    "$INSTDIR\PulseAmp.exe"
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

  ; Remove what the installer put there (never the whole folder blindly)
  Delete "$INSTDIR\PulseAmp.exe"
  Delete "$INSTDIR\yt-dlp.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\LICENSE.txt"
  RMDir /r "$INSTDIR\presets"
  RMDir /r "$INSTDIR\textures"
  RMDir /r "$INSTDIR\skins"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\uninstall.exe"
  RMDir  "$INSTDIR"

  ; Shortcuts
  Delete "$SMPROGRAMS\PulseAmp\PulseAmp.lnk"
  RMDir  "$SMPROGRAMS\PulseAmp"
  Delete "$DESKTOP\PulseAmp.lnk"

  ; File associations
  !insertmacro ForEachMediaExt RemoveOpenWith
  DeleteRegKey HKLM "Software\Classes\${PROG_ID}"
  DeleteRegKey HKLM "Software\Classes\Applications\PulseAmp.exe"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  ; Registry
  DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
