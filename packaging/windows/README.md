# Super-MPV-Player Windows assembly

The release uses one UCRT64 integrated core, with Windows processing and original-mapping entry points. No proprietary Dolby component, Omniphony engine/decoder bridge, libfreemkv helper, key database or device ICC is distributed.

## Build the source components

1. Use an MSYS2 UCRT64 environment with GCC, Meson and Ninja. Exact versions and source/build-recipe archives for the shipped dynamic libraries are in `docs/native-dependency-sources.json`; use those ABIs, not a blanket latest-library upgrade.
2. The companion source archive includes the required ASIO headers under `third_party/asio`, used under GPLv3. Git checkouts can obtain the pinned SDK from `sources.lock.json`. Run `packaging/windows/Build-Core.ps1` from an environment where UCRT64 compiler and Meson are on PATH. `core-build-options.json` records the original core build options, with the SDK path made portable. The Windows DV filter is part of the Windows source build, not a separate Meson option.
3. Build the Windows frame and RPU adapters as described in `windows/native-dv/README.md`. The source archive includes the pinned dovi_tool dependency at that README's relative path. Public Windows SDK/Visual Studio components remain external build dependencies.
4. Compile `MpvAnimeLauncher.cs` with the .NET Framework C# compiler using `/target:winexe /reference:System.Windows.Forms.dll /reference:System.Web.Extensions.dll /reference:System.Drawing.dll /out:MPV-Anime.exe`. The launcher source performs no automatic third-party installation.

These instructions and upstream recipes provide the source/build inputs; a fresh full dependency rebuild and bit-identical reproduction were not part of this packaging verification.

## Assemble and package

Create a separate task directory with `payload/`, `src/`, and `dist/`. Copy `make-files.py` to its root and `installer.nsi` to `src/`. Assemble the reviewed player runtime in `payload/`; the generator is not a downloader and does not fetch arbitrary binaries. Put the integrated core in the root, `windows-dv-experimental/` and `original-backend/`, with matching runtime DLLs. The native controls permit Windows processing only in the matching Windows-processing/root entry points. The `original-backend/` instance stays on MPV mapping.

Use the version-matched portable configuration and plugin sources, Java/BD-J runtime and notices. Preserve the previously packaged Windows uosc helper and Blu-ray launcher script with their corresponding source notices. Place the launcher source in `payload/package-source/` and the new release notes and feature guide in `payload/docs/`. In the release source checkout, get `$coreTree = git rev-parse HEAD:core/mpv`. In the assembly directory, run `python make-files.py --core-source-tree $coreTree`, then `makensis /INPUTCHARSET UTF8 src/installer.nsi`. The generator records the core source tree, creates per-file install/uninstall lists, and rejects bundled Omniphony engine/bridges, the freemkv helper, device profiles and known private-state files. Verify the installed payload and exercise launch, upgrade and uninstall before publishing.

The NSIS `TESTMODE` switch writes shortcuts inside the test installation and avoids production shortcut/registry changes. The public installer requires an ASCII installation directory for this Java/BD-J combination. It does not change file associations or start playback automatically.

Name the release assets `Super-MPV-Player-<version>-x64-Setup.exe`, `Super-MPV-Player-<version>-Source.zip`, and `Super-MPV-Player-<version>-Native-Sources.zip`. New installs default to `D:\Apps\Super-MPV-Player`. The prior install ID and registry key are retained to recognize upgrades; the installer does not remove shortcuts under the previous product name. The runtime filenames `mpv.exe`, `mpv.com`, and `MPV-Anime.exe` are unchanged.

Both launchers store resume records in this installation's `portable_config/watch_later`. They leave any global mpv resume records in place. An explicit `--watch-later-dir` passed to the Windows launcher overrides the portable default.

## 2.1.0: patched libplacebo

This branch now requires libplacebo 7.387.0 and the checked-in XYZ patch. The existing
2.0.0 release and its dependency manifest remain historical; candidate replacements
are listed in `docs/post-2.0.0-dependencies.json`.

In your build workspace, check out https://github.com/haasn/libplacebo at
`275f7dea27924cdd34ab2e6c20e448b6bc4fab65`. Apply
`packaging/windows/patches/libplacebo-7.387-xyz.patch` from the libplacebo source
root with `git apply --check <patch>` followed by `git apply <patch>`.
Build/install it with your UCRT64 compiler:

```powershell
meson setup <build-dir> <source-dir> --prefix=<install-prefix> --buildtype=release -Ddefault_library=shared -Dd3d11=enabled -Dopengl=enabled -Dvulkan=disabled -Dlcms=enabled -Ddovi=enabled -Dshaderc=enabled -Dglslang=disabled
meson compile -C <build-dir>
meson install -C <build-dir>
./packaging/windows/Build-Core.ps1 -LibplaceboPrefix <install-prefix> -BuildDir <mpv-build-dir>
```

Install the **patched** `libplacebo-387.dll` beside each resulting MPV executable.
Do not replace it with the unpatched compiler-environment DLL while staging.
The tested Windows candidate uses D3D11/OpenGL; Vulkan is disabled in this build.
The generic XYZ path needs both the MPV and libplacebo changes. It is not a
standalone Lua configuration fix. No title/CPL exceptions are shipped.

Playback results and the accepted known pause/resume drops are recorded in
`docs/release-2.1.0.md` and `docs/post-2.0.0-candidate.md`.
