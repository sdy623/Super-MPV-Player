# Open edition: source preview

Video and audio are independent choices. Windows Dolby video processing may run together with Omniphony object audio. Omniphony does not implement Dolby Vision video.

## Windows dependency
Use Microsoft Store or the device manufacturer to install a compatible licensed Dolby Vision extension. No Dolby extension binary, Store package, private credential or display ICC is supplied. `windows/native-dv/Detect-Dependencies.ps1` checks the current user's Store-signed package identity and status. Detection is not entitlement verification, support for every DV profile, or evidence of processing. The filter's runtime frame counters and fallback state remain authoritative.

P7 MEL RPU is adapted in memory to the Windows P8-compatible boundary. P8.4 retains HLG signaling. FEL is rejected by this boundary and remains on the original renderer. The tested internal panel target is SDR; this is not certified native DV HDR output. The user's original media is not rewritten.

## Omniphony
The engine and decoder bridges have separate licenses. This source preview does not redistribute bridge DLLs. Install compatible components from their maintainers after reviewing their licenses; missing components leave the original decoder intact. Headphone rendering should not be stacked unintentionally with Windows headphone spatial rendering. PCM/DCP keeps its original decoder. Neither video mode changes audio mode nor audio mode changes video mode.

## Build and release status
The exact integrated MPV source is in `core/mpv`; upstream revision pins are in `sources.lock.json`. Existing UI/config and plugin submodules are retained. Native bridge source is in `windows/native-dv`. See its build.cmd and RPU adapter instructions; Visual Studio and Windows SDK are build dependencies, not redistribution payloads.

This is a source preview, not an upgraded NSIS release. Still required before binary publication: complete reproducible dependency build, portable display defaults, full package license inventory, integrated UI/runtime tests, and antivirus review. The previous local playback evidence does not validate a new build. Do not redistribute the old local installer as this open edition.

## Theme helper and generic display defaults

The source tree excludes prebuilt ziggy executables. File-picker features need the matching official uosc helper in a runtime assembly. Configuration archives are not complete player installers. The open edition defaults to following the display; the optional SDR preset uses a 203-nit reference, not a measured panel luminance. No machine GPU name or personal ICC is imported by this upgrade.
