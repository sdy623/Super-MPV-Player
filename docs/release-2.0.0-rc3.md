# MPV Complete 2.0.0-rc3 — verification and limits

Windows x64 candidate, 2026-11-16. The configuration incorporates akFace/mpv.config main revision `51a11fd7111bb6fbbca0e2daab6c70da5145605d` through merge `e684f8c`; the integrated core identifies as `mpv v0.41.0-dev-anime-omniphony20261116`.

## What is included

Native MPV window and theme; Windows Dolby video adapter with official dependency detection; original video mapping; Omniphony integration; Blu-ray navigation/chapters/timeline; DCP CPL picture/audio association. The original-mapping entry uses the same source-backed core in a separate directory, where Windows Dolby processing is disabled. It is no longer the old third-party static build.

Omniphony engine/bridges and the freemkv helper are installed and managed by the user. Studio-first engine discovery is preserved. Missing Omniphony components restore the prior audio decoder; PCM/DCP keeps its original decoder. Local decrypted Blu-ray directories bypass freemkv. Video/audio choices are independent.

## Runtime observations

- P8.4 HLG sample: complete 199.008-second playback, two pause/resume cycles, Windows processing and external Omniphony active; 3 rendered-frame drops, 0 decoder drops in the low-overhead run.
- F1 P7 MEL: 180-second multi-position run, 16 chapters, reported duration 9318.308978 seconds, seek and two pause/resume cycles. Render-drop counter reached 7 early, reset at seek, then stayed at 0; decoder-drop counter stayed 0. This is not a full-film test or cumulative drop total across seeks.
- DCP: complete 276.166667-second playback with PCM audio, pause/resume and backward seek; 1 rendered-frame drop, 0 decoder drops.
- Blu-ray Java menu: actual initial menu and directional selection inspected; main-title chapter/timeline checks passed.
- Missing Omniphony bridge and missing engine: original decoder/audio restored in real runs. External components were provided only through test arguments for positive tests, not bundled or installed into the user's system.
- Source-backed original-mapping entry: actual HLG frame inspected, no Windows Dolby filter active, pause held and playback resumed.

The first diagnostic-heavy HLG run recorded 319 render drops, so it was not accepted as a performance pass. The later low-overhead run recorded 3; these observations do not isolate logging as the sole cause. Zero-drop playback under all configurations is not claimed.

## Installation and boundaries

Fresh installation, effective file comparison, no bundled external engines/bridges, launcher/dependency state, upgrade preservation, path rejection, and uninstall preservation were checked in isolated directories. The old installed player, Studio installation and original media were not replaced. Runtime executables/scripts were reused from the tested candidate; final packaging changes add notices and source references.

Use an ASCII installation directory, such as `D:\Apps\MPV-Complete`: this Java/BD-J combination has a known non-ASCII install-path limitation. There is no automatic file association, proprietary-component installation or protection bypass.

Windows processing currently supports the verified single-display/system-ICC setup; missing official extension, unsuitable display configuration or unavailable profile retains original mapping. The internal panel output is SDR, not physical native Dolby Vision HDR. P7 MEL adapts RPU in memory; FEL is not accepted by this Windows boundary. No original media is rewritten or whole-film transcoded.

The binaries are unsigned. A previous launcher was blocked by McAfee; vendor review has not returned. The verification machine has user-configured exclusions, so launch success does not establish an antivirus clean verdict. No exclusion or antivirus setting is changed by the installer.
