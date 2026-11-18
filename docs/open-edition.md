# Super-MPV-Player: Windows 2.2.0 pre-release

See the [release notes and known limits](release-2.2.0.md). Public builds are on [GitHub Releases](https://github.com/sdy623/Super-MPV-Player/releases).

On a compatible Windows setup, try Windows Dolby video processing first. [Advanced Dolby Vision SDR Rendering (gpu-next)](advanced-dv-hdr-sdr.md) is a separate open mode for eligible SDR-only displays. It needs no vendor extension and outputs SDR. Select it in the HDR menu for an open-path comparison. Omniphony is an independent audio choice and can run with either video path. See [profile and audio test coverage](playback-capabilities.md).

## Windows dependency
Install a compatible licensed Dolby Vision extension via Microsoft Store or the device manufacturer to use the Windows renderer. The installer does not distribute the extension, credentials or a display ICC. `windows/native-dv/Detect-Dependencies.ps1` checks the current user's installed package identity and status. Confirm processing in the HDR menu using runtime frame counters and the fallback state; the package check and the player's status label do not establish entitlement, a vendor logo or native Dolby Vision output.

Windows processing also needs a single display with a system ICC profile and the player's compatible bridge. It can process eligible HEVC P5 via the `dvhe.05` entry and eligible HEVC P8 or adapted P7 via `dvhe.08`. P9/AVC and P10/AV1 stay on the open renderer; they are outside this Windows bridge. If the required component, display setup or stream is not supported, playback stays on the open renderer or falls back to it.

The shipped `portable_config/script-opts/native_dv_controls.conf` sets `auto_enable=yes`, so the Windows-capable entry point attempts to select that route for eligible files. Ordinary fallback uses gpu-next. It does not select the separate Advanced SDR preset, which you can choose with **Ctrl+Alt+H**.

P7 MEL RPU is adapted in memory to the Windows P8-compatible boundary. P7 FEL first reconstructs the synchronized BL+EL contribution through the ABI-matched GPU helper, quantizes that result to P010 and adapts the RPU for the same Windows boundary. If the helper or frame contract is unavailable, the unmodified BL/EL remains on gpu-next. P8.4 retains HLG signaling. The tested internal panel target is SDR; this is not certified native DV HDR output. The user's original media is not rewritten.

## Omniphony: install the optional components yourself

With a compatible engine and bridge, Omniphony can render supported object-audio tracks, including Dolby TrueHD or Dolby Digital Plus carrying Atmos. Choose binaural headphones or speaker output. The supplied speaker example is stereo; adapt its layout to your configured speakers and audio device. This is local audio rendering, not HDMI bitstream passthrough. The engine and decoder bridges are separate downloads with their own licenses.

- [Omniphony / Studio v0.7.0 — official download](https://github.com/mgth/Omniphony/releases/tag/v0.7.0): choose `omniphony-studio-egui_0.7.0_x64-setup.exe` (Studio only), then start Studio once so it deploys the engine. Do not replace this player with the upstream player. This player uses liborender ABI 0.12.
- [harletty-bridge v0.9.0 — official download](https://github.com/harletty/harletty-bridge/releases/tag/v0.9.0): install the Windows x64 bridges separately; this pair uses bridge_api 0.6.x. Put the required `*_bridge.dll` files in `%LOCALAPPDATA%\omniphony\lib\`, or set your own `ad-orender-bridge-path`.
- Restart the player, then choose a mode in **HDR 与声音 → Omniphony 对象音频**. The same menu contains the official download links.

The upstream loader's behavior is preserved: without an explicit library override, a compatible Studio-deployed engine is preferred. The installer does not replace or reconfigure Studio. If the engine or bridge is unavailable, the player uses its normal decoder where one exists; IAMF requires a compatible bridge. Avoid stacking binaural output with Windows headphone spatial rendering. PCM/DCP keeps its original decoder.

## libfreemkv: user-managed optional backend

The installer does not include libfreemkv, a native freemkv bridge executable, KEYDB, certificates or activation data. The user handles installation, licensing, keys and updates. See the [maintainer documentation](https://freemkv.org/docs/aacs/).

To use the existing plugin interface, set `freemkv_path` in `portable_config/script-opts/bluray.conf` to an independently installed compatible `mpv-freemkv-bridge.exe`. The helper must implement the plugin's `serve --source --state-file --stop-file` interface; a standalone libfreemkv DLL or unrelated CLI is not interchangeable. The upstream site is documentation, not a claim that it distributes this custom helper. `freemkv_keydb` is an optional user-managed local path; no database is supplied or downloaded by the installer.

Without the helper, that backend reports unavailable. Decrypted BDMV playback and menus, libaacs, and detection of an installed MakeMKV remain available independently. Xreveal instance detection still disables player-side decryption. This helper is a sequential bridge, not a replacement for the local Blu-ray menu/timeline path.

## Original-mapping entry point

The public candidate uses the same source-backed integrated core for its original-mapping entry point, running from the separate `original-backend` directory. Windows Dolby processing remains unavailable in that instance; MPV handles normal video mapping. It does not redistribute the old third-party static MPV build. Existing local player installations and rollback copies are not replaced by this packaging change.

## Build and release status
The exact integrated MPV source is in `core/mpv`; upstream revision pins are in `sources.lock.json`. Existing UI/config and plugin submodules are retained. Native bridge source is in `windows/native-dv`. See its build.cmd and RPU adapter instructions; Visual Studio and Windows SDK are build dependencies, not redistribution payloads.

The pre-release uses the integrated core and merged portable configuration. See the [assembly instructions](../packaging/windows/README.md) and [exact native dependency sources](native-dependency-sources.json). The companion source archive provides the corresponding integrated and submodule sources, pinned RPU dependency and required ASIO headers. Vendor runtime notices and source-availability information are included with the installer. See the [release notes](release-2.2.0.md) for the checks and limitations of this particular build.

## Theme helper and generic display defaults

The source tree excludes prebuilt ziggy executables. File-picker features need the matching official uosc helper in a runtime assembly. Configuration archives are not complete player installers. The open edition defaults to following the display; the optional SDR preset uses a 203-nit reference, not a measured panel luminance. No machine GPU name or personal ICC is imported by this upgrade.


## 2.1.0 changes
Version 2.1.0 introduced explicit PQ/HLG GPU transport, FP16 HLG output, generic
XYZ/ICM DCP playback and pre-roll-aware DV seeks. See [candidate status](post-2.0.0-candidate.md) for measured improvements
and remaining playback failures. Official Dolby dependency detection, separate
Omniphony/libfreemkv installation and original mapping remain required contracts.
