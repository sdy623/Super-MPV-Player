# Windows DV adapter build

The adapter uses public Windows Media Foundation / MediaEngine interfaces. Dolby processing is provided by the user's installed component; no proprietary implementation is included.

1. Install Visual Studio C++ build tools and the Windows SDK through Microsoft.
2. Clone https://github.com/quietvoid/dovi_tool.git into this directory as `dovi_tool` and check out `614c816b6446dcd1dbaf433403d499a6026fbb5a`.
3. From `rpu_adapter`, run `cargo test --locked`, then `cargo build --release --locked`. Public upstream synthetic fixtures are used; optional movie fixtures remain excluded/ignored.
4. In an x64 Native Tools prompt, run `build.cmd` from this directory. The generated DLL and RPU DLL belong beside each other.
5. Build `core/mpv` with the exact FFmpeg/libplacebo ABI and options recorded in sources.lock.json. The integration needs a compatible UCRT64 toolchain and the Windows DV filter enabled; these are not supplied by a configuration ZIP. A fully reproducible dependency build and new binary release are still pending.
6. A runtime assembly places the bridge DLLs, native-controls.lua, dependency-policy.lua and Detect-Dependencies.ps1 in `windows-dv-experimental`, with the portable_config loader. The source repository directory is not itself an installed player.

Detection checks the current user's officially signed Store package, not license entitlement. If OEM deployment uses a different package identity, it remains unsupported until that identity and interface are verified; do not weaken the check to match arbitrary DLL names. Rendering is verified separately by filter state/counters. Missing dependencies preserve the original renderer.

The P7 MEL RPU adaptation is in-memory. FEL is rejected; no silent enhancement-layer deletion. P8.4 uses HLG input signaling. SDR output is not native DV HDR display certification.
