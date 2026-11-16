# Windows DV adapter build

The adapter uses public Windows Media Foundation / MediaEngine interfaces. Dolby processing is provided by the user's installed component; no proprietary implementation is included.

1. Install Visual Studio C++ build tools and the Windows SDK through Microsoft.
2. Clone https://github.com/quietvoid/dovi_tool.git into this directory as `dovi_tool` and check out `614c816b6446dcd1dbaf433403d499a6026fbb5a`.
3. From `rpu_adapter`, run `cargo test --locked`, then `cargo build --release --locked`. Public upstream synthetic fixtures are used; optional movie fixtures remain excluded/ignored.
4. In an x64 Native Tools prompt, run `build.cmd` from this directory. The generated DLL and RPU DLL belong beside each other.
5. Build `core/mpv` using `packaging/windows/Build-Core.ps1`. The recorded options and exact dependency source/recipe links are in `packaging/windows/core-build-options.json` and `docs/native-dependency-sources.json`. The Windows build includes the DV filter. A configuration ZIP alone is not a player runtime.
6. A runtime assembly places the bridge DLLs, native-controls.lua, dependency-policy.lua and Detect-Dependencies.ps1 in `windows-dv-experimental`, with the portable_config loader. The source repository directory is not itself an installed player.

Detection checks the current user's officially signed Store package, not license entitlement. If OEM deployment uses a different package identity, it remains unsupported until that identity and interface are verified; do not weaken the check to match arbitrary DLL names. Rendering is verified separately by filter state/counters. Missing dependencies preserve the original renderer.

The P7 MEL RPU adaptation is in-memory. FEL is rejected; no silent enhancement-layer deletion. P8.4 uses HLG input signaling. SDR output is not native DV HDR display certification.

## GPU transport for high-frame-rate P8.4

HLG inputs at 50fps or more use D3D11 P010 surfaces when the original HLG/BT.2020/limited-range/RPU contract is valid. MediaEngine owns a separate device on the same DXGI adapter. NT shared textures and keyed mutexes transfer P010/BGRA pixels on GPU; no per-frame pixel readback is used on this path. Exact PTS and original RPU signaling are retained. Bounded ordered prefetch absorbs short TransferVideoFrame stalls. The CPU/P7 path is retained. UI status requires actual GPU counters and output format; extension presence alone is insufficient.

See [2.0.0 validation](../../docs/release-2.0.0.md) for full-clip audio, pause and menu checks.
