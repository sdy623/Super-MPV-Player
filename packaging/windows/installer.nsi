Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!ifndef PAYLOAD
!define PAYLOAD "..\payload"
!endif
!ifndef OUTPUT
!define OUTPUT "..\dist\MPV-Complete-2.0.0-rc3-x64-Setup.exe"
!endif
!define PRODUCT "MPV Complete"
!define OWNER "MPVComplete-NSIS-v2"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MPVComplete.NSIS"
Name "${PRODUCT}"
OutFile "${OUTPUT}"
InstallDir "D:\Apps\MPV-Complete"
InstallDirRegKey HKCU "${REGKEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 8
SetDatablockOptimize on
ShowInstDetails show
ShowUninstDetails show
BrandingText "MPV Complete - integrated media player"
Icon "${PAYLOAD}\installer\mpv-icon.ico"
UninstallIcon "${PAYLOAD}\installer\mpv-icon.ico"
VIProductVersion "2.0.0.3"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "FileDescription" "Per-user MPV installer with no-console launcher"
VIAddVersionKey "FileVersion" "2.0.0.3"
VIAddVersionKey "LegalCopyright" "Third-party components retain their respective licenses"
Var TestMode
Var Links
Var InstallError
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
    SetShellVarContext current
    StrCpy $TestMode "0"
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} "$0" "/TESTMODE" $1
    ${IfNot} ${Errors}
        StrCpy $TestMode "1"
    ${EndIf}
FunctionEnd

Function VerifyDestination
    ; Lexical normalization must also accept a directory that does not exist yet.
    System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i .r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
        StrCpy $InstallError "Invalid or too-long destination path."
        Goto reject
    ${EndIf}
    StrCpy $INSTDIR $0
    System::Alloc 4096
    Pop $2
    System::Call 'kernel32::WideCharToMultiByte(i 20127, i 0, w "$INSTDIR", i -1, p r2, i 4096, p 0, *i .r1) i .r3'
    System::Free $2
    ${If} $3 == 0
    ${OrIf} $1 != 0
        StrCpy $InstallError "This build requires an English/ASCII installation folder for Blu-ray Java menus. Example: D:\Apps\MPV-Complete"
        Goto reject
    ${EndIf}
    StrLen $0 $INSTDIR
    ${If} $0 < 8
        StrCpy $InstallError "Select a dedicated application folder, not a drive root."
        Goto reject
    ${EndIf}
    IfFileExists "$INSTDIR\install-owner.ini" owned 0
    IfFileExists "$INSTDIR" 0 done
    ; A wildcard proves directory existence, not that it contains user files.
    IfFileExists "$INSTDIR\*.*" scan not_directory
scan:
    ClearErrors
    FindFirst $0 $1 "$INSTDIR\*.*"
    IfErrors unreadable 0
scan_next:
    StrCmp $1 "" empty
    StrCmp $1 "." skip_entry
    StrCmp $1 ".." skip_entry
    FindClose $0
    Goto unowned
skip_entry:
    FindNext $0 $1
    Goto scan_next
empty:
    FindClose $0
    Goto done
owned:
    ReadINIStr $0 "$INSTDIR\install-owner.ini" "Install" "Owner"
    StrCmp $0 "${OWNER}" done unowned
not_directory:
    StrCpy $InstallError "Destination is an existing file. Select a new folder."
    Goto reject
unreadable:
    StrCpy $InstallError "Cannot inspect the destination. Check folder permissions."
    Goto reject
unowned:
    StrCpy $InstallError "Existing non-empty unowned folder refused. Select a new folder; the old player is preserved."
reject:
    DetailPrint "$InstallError"
    SetErrorLevel 2
    Abort "$InstallError"
done:
FunctionEnd

Section "Player" SEC_PLAYER
    Call VerifyDestination
    SetOutPath "$INSTDIR"
    ClearErrors
    !include "install-files.nsh"
    ${If} ${Errors}
        SetErrorLevel 3
        Abort "文件安装失败。请检查磁盘空间、占用或安全软件记录。"
    ${EndIf}
    WriteINIStr "$INSTDIR\install-owner.ini" "Install" "Owner" "${OWNER}"
    WriteINIStr "$INSTDIR\install-owner.ini" "Install" "TestMode" "$TestMode"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ${If} $TestMode == "1"
        StrCpy $Links "$INSTDIR\integration-test"
    ${Else}
        StrCpy $Links "$SMPROGRAMS\MPV Complete"
    ${EndIf}
    CreateDirectory "$Links"
    CreateShortcut "$Links\MPV Complete.lnk" "$INSTDIR\MPV-Anime.exe" "" "$INSTDIR\MPV-Anime.exe" 0 SW_SHOWNORMAL
    CreateShortcut "$Links\蓝光原盘文件夹.lnk" "$INSTDIR\MPV-Anime.exe" "--open-bd-folder" "$INSTDIR\MPV-Anime.exe" 0 SW_SHOWNORMAL
    CreateShortcut "$Links\蓝光光驱播放.lnk" "$INSTDIR\MPV-Anime.exe" "--open-bd-drive" "$INSTDIR\MPV-Anime.exe" 0 SW_SHOWNORMAL
    CreateShortcut "$Links\MPV Original Backend.lnk" "$INSTDIR\MPV-Anime.exe" "--original-backend" "$INSTDIR\MPV-Anime.exe" 0 SW_SHOWNORMAL
    ${If} $TestMode != "1"
        CreateShortcut "$DESKTOP\MPV Complete.lnk" "$INSTDIR\MPV-Anime.exe" "" "$INSTDIR\MPV-Anime.exe" 0 SW_SHOWNORMAL
        WriteRegStr HKCU "${REGKEY}" "DisplayName" "${PRODUCT}"
        WriteRegStr HKCU "${REGKEY}" "DisplayVersion" "2.0.0-rc3"
        WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
        WriteRegStr HKCU "${REGKEY}" "DisplayIcon" "$INSTDIR\MPV-Anime.exe"
        WriteRegStr HKCU "${REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
        WriteRegStr HKCU "${REGKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
        WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
        WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
    ${EndIf}
SectionEnd

Function un.onInit
    SetShellVarContext current
    ReadINIStr $0 "$INSTDIR\install-owner.ini" "Install" "Owner"
    ${If} $0 != "${OWNER}"
        SetErrorLevel 2
        Quit
    ${EndIf}
    ReadINIStr $TestMode "$INSTDIR\install-owner.ini" "Install" "TestMode"
FunctionEnd

Section "Uninstall"
    !include "uninstall-files.nsh"
    ${If} $TestMode == "1"
        StrCpy $Links "$INSTDIR\integration-test"
    ${Else}
        StrCpy $Links "$SMPROGRAMS\MPV Complete"
        ReadRegStr $0 HKCU "${REGKEY}" "InstallLocation"
        ${If} $0 == $INSTDIR
            DeleteRegKey HKCU "${REGKEY}"
            Delete "$DESKTOP\MPV Complete.lnk"
        ${EndIf}
    ${EndIf}
    Delete "$Links\MPV Complete.lnk"
    Delete "$Links\MPV Original Backend.lnk"
    Delete "$Links\蓝光原盘文件夹.lnk"
    Delete "$Links\蓝光光驱播放.lnk"
    RMDir "$Links"
    Delete "$INSTDIR\install-owner.ini"
    Delete "$INSTDIR\Uninstall.exe"
    ; No recursive deletion: preserve user settings, caches, and unknown files.
    RMDir "$INSTDIR"
SectionEnd
