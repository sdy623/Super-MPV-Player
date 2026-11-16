# MPV Complete 2.0.0 — GPU Dolby processing release

This Windows x64 release promotes the tested GPU transport repair, rather than repackaging the RC3 CPU round-trip implementation. The existing upstream configuration merge, theme, native MPV window, Blu-ray navigation, chapters, timeline, DCP CPL association and Omniphony integration are retained.

## What changed

For eligible 50+fps P8.4/HLG D3D11 P010 input, MediaEngine owns a separate device on the same graphics adapter. NT shared textures and keyed mutex synchronization keep picture data on GPU. The original HLG/BT.2020/limited-range and RPU signaling, frame duration and exact PTS are preserved. Bounded ordered output prefetch absorbs short TransferVideoFrame stalls. Windows real-time mode is enabled on this GPU path; CPU/P7 processing is retained. Color parameters were not retuned to hide the performance issue.

The status menu recognizes processed GPU BGRA only with matching transport and frame counters. Official component presence alone never means frames were processed. This remains Windows Dolby processing followed by SDR presentation on the tested internal panel, not certified native DV HDR output.

## Verification

- Freshly installed 2.0.0 NSIS package: the complete 116.512-second 4K60 P8.4/HLG test clip, all 6938 video frames processed by Windows through GPU transport; zero rendered and decoder drops. WASAPI EAC3 48kHz audio remained active. Three 2.5-second pauses held position and resumed with zero added drops. Maximum sampled software A/V error was 0.000026 seconds.
- The identical core/bridge was also tested full-screen to EOF: 2 isolated rendered drops around80 seconds, zero decoder drops; three 10-second pauses produced no resume drops. This is not a claim of zero drops in every configuration.
- GPU queue reset: forward seek to60 seconds and backward seek to5 seconds preserved playback and A/V sync, without device loss or fallback.
- F1 P7 MEL regression with the same core/bridge: paired EL/PQ preserved, pause/resume, seek to3600 seconds and chapter advancement passed; reported duration9318.308978 seconds and16 chapters. The 75-second bounded test had zero rendered/decoder drops. This was not a full-film test.
- Fresh 2.0.0 install: Blu-ray Java main menu was rendered and directional navigation from Play to Scene Selections was visually inspected. An unsupported RPU in an earlier disc-intro segment retained original rendering; the main menu and tested P7 title used Windows processing. No unsupported profile was relabeled.
- Fresh 2.0.0 install, 30-second integration checks: external Omniphony decoded EAC3 objects with the Windows GPU video path active, including pause/resume. The DCP folder associated picture and PCM audio automatically, retained its 276.166667-second timeline and original PCM decoder when Omniphony was selected; pause/resume and actual pictures were checked. External engines/bridges were not copied into the package.
- Installer lifecycle: fresh install, byte-for-byte comparison of6945 manifested files, shortcut targets, actual launcher/dependency state, upgrade preserving configuration, rejection of unsupported Unicode/unowned paths, and uninstall preserving user files all passed in isolated test directories.
- Four Lua suites cover official dependency policy, CPU and GPU processing status, SDR output policy, independent Omniphony choices and missing-component recovery. Eight Python source-release checks passed.

The performance candidates which lost the GPU device or dropped frames after resume were rejected and are not this release. The published runtime is the accepted shared-texture/prefetch candidate.

## Components remain user-installed

Dolby proprietary components must come from Microsoft Store or the device manufacturer. The public package keeps the official signed-package detector and the verified single-display/system-ICC requirement. No Dolby binary, activation material, device ICC, Omniphony engine/decoder bridge, libfreemkv helper or key database is bundled. Missing dependencies retain original rendering/decoding.

Omniphony retains Studio-first discovery; user-selected audio and video modes are independent. PCM/DCP keeps its original decoder. Avoid unintentionally stacking headphone rendering with Windows headphone spatial rendering. Download links and compatible versions are in [open-edition.md](open-edition.md).

## Installation, source and limits

Use an ASCII installation directory such as `D:\Apps\MPV-Complete`; this Java/BD-J runtime still has a non-ASCII installation-path limitation. Launching the player does not open a console window. The installer does not add antivirus exclusions, change file associations, install proprietary components, or overwrite an unowned existing player folder.

The binaries are unsigned. Earlier builds triggered McAfee; vendor review remains unresolved, and this test computer has user-configured exclusions. Functional launch tests are not an antivirus-clean certification.

The Source archive includes the matching core, bridge, configuration, plugin submodules, pinned RPU dependency and ASIO build headers. Native-Sources contains the unchanged dependency-source/build-recipe collection used by RC3. Existing source provenance and vendor notices remain included. A full fresh dependency rebuild and bit-identical reproduction are not claimed.
