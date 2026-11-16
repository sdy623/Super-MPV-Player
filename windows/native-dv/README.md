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

## GPU transport, precision and seeking

The current source extends D3D11 shared textures to validated PQ and HLG inputs,
including P7 MEL after BL/EL pairing. The v6 ABI explicitly distinguishes PQ from
HLG; v5 remains HLG-only. The opt-in v7 HLG path returns FP16 scRGB so negative and
above-one linear values survive until final display mapping. PQ/P7 retains the
established BGRA output and explicit output-ICC handling. Official dependency
checks and the original-mapping entry point remain intact.

FEL stays in gpu-next with the upgraded libplacebo reconstruction path; Windows
BL-only processing is not mislabeled as FEL reconstruction. Source RPU/EL is not
removed from fallback frames. Actual frame counters and output format determine
UI status, not just an installed extension.

During ordinary precise seeks, the core signals the target to the Windows filter.
HEVC decoding and RPU/EL pairing still run, but invisible decoded pre-roll is not
sent through the expensive renderer. Only its latest frame is retained for EOF;
the requested frame and final-frame fallback render normally. Reverse playback,
backstep/very-exact and `hr-seek-framedrop=no` retain their original behavior.

See [candidate verification](../../docs/post-2.0.0-candidate.md). The historical
[2.0.0 validation](../../docs/release-2.0.0.md) describes the already-published binary.
