# Open edition: Windows release candidate

Video and audio are independent choices. Windows Dolby video processing may run together with Omniphony object audio. Omniphony does not implement Dolby Vision video.

## Windows dependency
Use Microsoft Store or the device manufacturer to install a compatible licensed Dolby Vision extension. No Dolby extension binary, Store package, private credential or display ICC is supplied. `windows/native-dv/Detect-Dependencies.ps1` checks the current user's Store-signed package identity and status. Detection is not entitlement verification, support for every DV profile, or evidence of processing. The filter's runtime frame counters and fallback state remain authoritative.

The Windows-processing candidate currently requires a single display with a system ICC profile. It reads that profile at runtime instead of distributing a device-specific profile; other setups retain the original mapping.

P7 MEL RPU is adapted in memory to the Windows P8-compatible boundary. P8.4 retains HLG signaling. FEL is rejected by this boundary and remains on the original renderer. The tested internal panel target is SDR; this is not certified native DV HDR output. The user's original media is not rewritten.

## Omniphony: install the optional components yourself

The player includes the integration, not the engine or decoder bridge DLLs. They have separate licenses. Review each maintainer's license before installing.

- [Omniphony / Studio v0.7.0 — official download](https://github.com/mgth/Omniphony/releases/tag/v0.7.0): choose `omniphony-studio-egui_0.7.0_x64-setup.exe` (Studio only), then start Studio once so it deploys the engine. Do not replace this player with the upstream player. This player uses liborender ABI 0.12.
- [harletty-bridge v0.9.0 — official download](https://github.com/harletty/harletty-bridge/releases/tag/v0.9.0): install the Windows x64 bridges separately; this pair uses bridge_api 0.6.x. Put the required `*_bridge.dll` files in `%LOCALAPPDATA%\omniphony\lib\`, or set your own `ad-orender-bridge-path`.
- Restart the player, then choose a mode in **HDR 与声音 → Omniphony 对象音频**. The same menu contains the official download links.

The upstream loader's behavior is preserved: without an explicit library override, a compatible Studio-deployed engine is preferred. The installer does not replace or reconfigure Studio. Missing or incompatible components keep the original audio decoder. Headphone rendering should not be stacked unintentionally with Windows headphone spatial rendering. PCM/DCP keeps its original decoder. Neither video mode changes audio mode nor audio mode changes video mode.

## libfreemkv: user-managed optional backend

The installer does not include libfreemkv, a native freemkv bridge executable, KEYDB, certificates or activation data. The user handles installation, licensing, keys and updates. See the [maintainer documentation](https://freemkv.org/docs/aacs/).

To use the existing plugin interface, set `freemkv_path` in `portable_config/script-opts/bluray.conf` to an independently installed compatible `mpv-freemkv-bridge.exe`. The helper must implement the plugin's `serve --source --state-file --stop-file` interface; a standalone libfreemkv DLL or unrelated CLI is not interchangeable. The upstream site is documentation, not a claim that it distributes this custom helper. `freemkv_keydb` is an optional user-managed local path; no database is supplied or downloaded by the installer.

Without the helper, that backend reports unavailable. Decrypted BDMV playback and menus, libaacs, and detection of an installed MakeMKV remain available independently. Xreveal instance detection still disables player-side decryption. This helper is a sequential bridge, not a replacement for the local Blu-ray menu/timeline path.

## Original-mapping entry point

The public candidate uses the same source-backed integrated core for its original-mapping entry point, running from the separate `original-backend` directory. Windows Dolby processing remains unavailable in that instance; MPV handles normal video mapping. It does not redistribute the old third-party static MPV build. Existing local player installations and rollback copies are not replaced by this packaging change.

## Build and release status
The exact integrated MPV source is in `core/mpv`; upstream revision pins are in `sources.lock.json`. Existing UI/config and plugin submodules are retained. Native bridge source is in `windows/native-dv`. See its build.cmd and RPU adapter instructions; Visual Studio and Windows SDK are build dependencies, not redistribution payloads.

The Windows 2.0.0-rc3 candidate packages the tested integrated core and merged configuration. See [verification and limitations](release-2.0.0-rc3.md), [assembly instructions](../packaging/windows/README.md), and [exact native dependency sources](native-dependency-sources.json). The companion source archive includes the integrated sources, submodule sources, pinned RPU dependency and required ASIO headers. Vendor runtime notices and source-availability information remain in the installer. Fresh full dependency recompilation and bit-identical reproduction were not performed during packaging. Antivirus vendor review remains pending; functional launch success is not a clean antivirus verdict.

## Theme helper and generic display defaults

The source tree excludes prebuilt ziggy executables. File-picker features need the matching official uosc helper in a runtime assembly. Configuration archives are not complete player installers. The open edition defaults to following the display; the optional SDR preset uses a 203-nit reference, not a measured panel luminance. No machine GPU name or personal ICC is imported by this upgrade.
