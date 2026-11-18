# Windows DV adapter build

The adapter uses public Windows Media Foundation / MediaEngine interfaces. Dolby processing is provided by the user's installed component; no proprietary implementation is included.

1. Install Visual Studio C++ build tools and the Windows SDK through Microsoft.
2. Clone https://github.com/quietvoid/dovi_tool.git into this directory as `dovi_tool` and check out `614c816b6446dcd1dbaf433403d499a6026fbb5a`.
3. From `rpu_adapter`, run `cargo test --locked`, then `cargo build --release --locked`. Public upstream synthetic fixtures are used; optional movie fixtures remain excluded/ignored.
4. In an x64 Native Tools prompt, run `build.cmd` from this directory. The generated DLL and RPU DLL belong beside each other.
5. Build `core/mpv` using `packaging/windows/Build-Core.ps1`. The recorded options and exact dependency source/recipe links are in `packaging/windows/core-build-options.json` and `docs/native-dependency-sources.json`. The Windows build includes the DV filter. A configuration ZIP alone is not a player runtime.
6. A runtime assembly places the bridge DLLs, native-controls.lua, dependency-policy.lua and Detect-Dependencies.ps1 in `windows-dv-experimental`, with the portable_config loader. The source repository directory is not itself an installed player.

Detection checks the current user's officially signed Store package, not license entitlement. If OEM deployment uses a different package identity, it remains unsupported until that identity and interface are verified; do not weaken the check to match arbitrary DLL names. Rendering is verified separately by filter state/counters. Missing dependencies preserve the original renderer.

The P7 MEL RPU adaptation is in-memory. P7 FEL automatically tries the separate, ABI-matched reconstruction helper; a missing or incompatible helper preserves original BL/EL and falls back to gpu-next. P8.4 uses HLG input signaling. SDR output is not native DV HDR display certification.

## GPU transport, precision and seeking

The current source extends D3D11 shared textures to validated PQ and HLG inputs,
including P7 MEL after BL/EL pairing. The v6 ABI explicitly distinguishes PQ from
HLG; v5 remains HLG-only. P8.1/P8.4 now use the separate v12 native SDR
contract: PQ and HLG inputs stay distinct, while the installed effect declares
A2R10G10B10 / sRGB / BT.709 / full-range output. The D3D11 handoff copies those
unchanged normalized sRGB code values into an FP16 transport surface; it adds no
second OETF or exposure gain. A direct RGB10 handoff was rejected after it changed
the effective P8.4 presentation and made highlights visibly too bright.
The older HLG-only v9 relative-linear FP16 path and v7 scRGB ABI remain available
for their existing callers. P7 keeps its established BGRA output and explicit
output-ICC handling. Official dependency checks and original mapping remain intact.

The native effect host opens the official effect's advertised app service, creates the
required DispatcherQueue, and passes DisplayInformation through SetOutputDevice.
This candidate supports one active display and the verified Dolby Store package
identity. Connection closure precedes effect destruction; the queue is drained
before COM/MF shutdown. A service, media-type or timestamp failure preserves the
original frames instead of reporting successful Windows processing.

`output-contract=windows-sdr10-fp16-transport` identifies the accepted native SDR
code-value handoff. During this direct-presentation mode MPV clears its
explicit target ICC and disables automatic target ICC loading, then restores the
previous settings on exit. P8 uses explicit piecewise sRGB on both renderer sides;
P5 retains its accepted transfer policy. No gamut-mapping adjustment compensates
for an input/output color mismatch. Target brightness remains `auto`.

This is an output-contract policy, not a profile-wide rule that ICC must be off.
The older `display-relative-srgb-fp16` path retains its own display-ICM behavior.
DCP keeps XYZ and its ICM path; original mapping keeps its settings. The native
SDR output class matches the supplied Vivid logs, and the user accepted P5 and
P8.4 appearance on the tested internal SDR display. This does not establish that
Dolby's component applies the factory ICC LUT internally or certify other panels.

HDR controls distinguish relative FP16 from absolute scRGB. The Dolby logo is
supplied by the installed component, not drawn by this adapter. P7 FEL uses the guarded GPU reconstruction helper before the Windows handoff when its ABI matches; metadata active-area offsets are not turned into geometry cropping.

Automatic enablement waits for official dependency detection before releasing the
preloaded video to its playback clock. This avoids inserting the filter after the
first frames have already started playing.

Without the matching helper, FEL stays in gpu-next with the upgraded libplacebo reconstruction path; Windows BL-only processing is not mislabeled as FEL reconstruction. Source RPU/EL is not
removed from fallback frames. Actual frame counters and output format determine
UI status, not just an installed extension.

Shared surfaces use D3D11.4 GPU fence `Wait`/`Signal` when both devices support
shared fences. Older devices keep the checked keyed-mutex path. Eight rotating
input/output slots prevent a single-frame job from repeatedly serializing on the
same transfer surface. Producer completion, consumer copies and slot reuse are
ordered on the GPU; pixels do not take a CPU readback detour. The existing
12-frame ready limit is unchanged, and batches now use its available slots.
The transfer ring is bounded. Native SDR output slots retain the accepted FP16
code-value container; decoder and MediaEngine allocations remain separate. Allocation
or rendering failure keeps the original frame/fallback path.

During ordinary precise seeks, the core signals the target to the Windows filter.
HEVC decoding and RPU/EL pairing still run, but invisible decoded pre-roll is not
sent through the expensive renderer. Only its latest frame is retained for EOF;
the requested frame and final-frame fallback render normally. Reverse playback,
backstep/very-exact and `hr-seek-framedrop=no` retain their original behavior.

See [candidate verification](../../docs/post-2.0.0-candidate.md). The historical
[2.0.0 validation](../../docs/release-2.0.0.md) describes the already-published binary.


## P5 / P8 native SDR and guarded P7 FEL

P5 has an independent `dvhe.05` entry with full-range raw P010 and unchanged P5 RPU. For the tested reference, input VUI matches the Windows decoder (unknown transfer/primaries, matrix absent); it must not be treated as ordinary BT.2020 NCL PQ. The effect declares SDR10 sRGB output, copied unchanged into the accepted FP16 code-value transport without an extra OETF. The MPV ICC bypass follows the native SDR output contract, shared with P8.1/P8.4, rather than profile ID. Leaving this path restores the preceding ICC settings. General repository mode uses Advanced color management for the open route and releases it whenever the Windows processor owns the frame.

P8.1 and P8.4 enter `wdv_create_gpu_sdr_v12` with original P8 RPU and no enhancement layer. The entry accepts explicit PQ or HLG only. A route-state key prevents reuse of a P7 PQ session as a P8.1 native SDR session. P7/FEL never enters v12; P5 retains its v11 entry. The dependency probe records `dvav.09` and `dav1.10` separately from bridge support: P9/AVC and P10/AV1 stay on FFmpeg/libplacebo because this bridge carries HEVC NAL62 metadata, not AVC RPU transport or AV1 EMDF.

P7 FEL: the packaged default sets `fel-experiment=yes`. It requires `fel-gpu.dll`, the matching custom libplacebo API and libdovi. It reconstructs BL+EL on the GPU before quantizing to P010 and adapting RPU for Windows; it is not a lossless 12-bit P7 conversion. The ABI check and frame contract fail closed: if the helper is missing, incompatible or cannot process the paired EL, the original BL/EL remains on gpu-next rather than entering a BL-only Windows path. Its ABI header is mirrored in the MPV filter directory for independent builds.

Additional helper sources are `src/profile_transport.c` (P5 classifier/transport) and `src/fel_gpu.c`. In the matching UCRT64 environment with libdovi 3.4 headers/library installed:

```sh
cc -shared -O2 src/profile_transport.c -o dv-profile.dll -ldovi
cc -shared -O2 src/fel_gpu.c -o fel-gpu.dll -lplacebo -ldovi -ld3d11 -ldxgi
```

The FEL helper must use the same libplacebo API/build as the MPV core; the ABI rejects a mismatch. These commands do not fetch or redistribute proprietary Dolby components. Keep the helpers and their open-source runtime dependencies beside `windows-dv-frames.dll`. This is a source checkpoint, not an automatically assembled release package.

See [native SDR verification](../../docs/native-sdr-profile-verification.md) and the [pre-v12 profile/output/ICM audit](../../docs/dv-profile-color-contract-audit.md).
