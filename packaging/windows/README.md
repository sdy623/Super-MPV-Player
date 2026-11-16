# Windows candidate assembly

The release uses one UCRT64 integrated core, with Windows processing and original-mapping entry points. No proprietary Dolby component, Omniphony engine/decoder bridge, libfreemkv helper, key database or device ICC is distributed.

## Build the source components

1. Use an MSYS2 UCRT64 environment with GCC, Meson and Ninja. Exact versions and source/build-recipe archives for the shipped dynamic libraries are in `docs/native-dependency-sources.json`; use those ABIs, not a blanket latest-library upgrade.
2. The companion source archive includes the required ASIO headers under `third_party/asio`, used under GPLv3. Git checkouts can obtain the pinned SDK from `sources.lock.json`. Run `packaging/windows/Build-Core.ps1` from an environment where UCRT64 compiler and Meson are on PATH. `core-build-options.json` records the original core build options, with the SDK path made portable. The Windows DV filter is part of the Windows source build, not a separate Meson option.
3. Build the Windows frame and RPU adapters as described in `windows/native-dv/README.md`. The source archive includes the pinned dovi_tool dependency at that README's relative path. Public Windows SDK/Visual Studio components remain external build dependencies.
4. Compile `MpvAnimeLauncher.cs` with the .NET Framework C# compiler using `/target:winexe /reference:System.Windows.Forms.dll /reference:System.Web.Extensions.dll /out:MPV-Anime.exe`. The launcher source performs no automatic third-party installation.

These instructions and upstream recipes provide the source/build inputs; a fresh full dependency rebuild and bit-identical reproduction were not part of this packaging verification.

## Assemble and package

Create a separate task directory with `payload/`, `src/`, and `dist/`. Copy `make-files.py` to its root and `installer.nsi` to `src/`. Assemble the reviewed player runtime in `payload/`; the generator is not a downloader and does not fetch arbitrary binaries. Put the integrated core in the root, `windows-dv-experimental/` and `original-backend/`, with matching runtime DLLs. The native controls permit Windows processing only in the matching Windows-processing/root entry points. The `original-backend/` instance stays on MPV mapping.

Use the same portable configuration and plugin sources, Java/BD-J runtime and notices as the candidate manifest. Place the launcher source in `payload/package-source/`. Run `python make-files.py`, then `makensis src/installer.nsi`. It generates per-file install/uninstall lists and rejects bundled Omniphony engine/bridges, the freemkv helper, device profiles and known private-data filenames. Fresh-install verification must compare the effective payload and exercise launch/upgrade/uninstall; do not treat compilation alone as playback validation.

The NSIS `TESTMODE` switch writes shortcuts inside the test installation and avoids production shortcut/registry changes. The public installer requires an ASCII installation directory for this Java/BD-J combination. It does not change file associations or start playback automatically.
