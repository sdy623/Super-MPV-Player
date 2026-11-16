# Post-2.0.0 source candidate

## Implemented and checked

- PQ/HLG shared-GPU input uses the actual base transfer. HLG FP16 scRGB preserves
  highlights and negative/out-of-gamut values until final display mapping.
- P7 MEL retains paired enhancement metadata; FEL uses gpu-next reconstruction.
- Generic 12-bit XYZ/ST428 DCP rendering preserves XYZ colorimetry by default and
  uses the system display ICM. Cinema-white adaptation is optional. Direct MXF and
  CPL inputs share the same path; there are no Ray/CPL-specific rules.
- CPL sound pairing, chapters, native window and theme remain. DCP cannot enable
  Windows Dolby processing. JPEG2000 has a file-local bounded decode queue.
- DV seek pre-roll is decoded/paired but not needlessly rendered. A paused HLG
  seek measured14.85s before and0.39s after; rapid drag0.45s. PQ/P7 seeks, final-frame
  display and return/resume passed with no fallback or dropped frames in bounded
  tests. P7 paired EL remained present.
- Same HLG target-frame screenshot mean difference0.024 on an8-bit-equivalent
  scale; color/transfer settings were not changed by the seek fix.
- Detailed stats report source format, RPU, actual Windows/GPU/FEL processing and
  SDR output together. Official dependency checks and generic display defaults
  remain in the open source UI; local device profiles are not bundled.

## Pause/resume verification

The first complete 116.512 s HLG run had 8 output drops. Diagnostics separated
shared-surface waits from the caller context lock. Explicit flush and timer
precision probes did not resolve the issue. GPU fences alone and the ready-slot
fix still had 3 output drops; those results remain in the local record.

With rotating GPU transfer surfaces plus GPU fences and bounded ready-slot use,
the complete HLG clip passed with **0 output and 0 decoder drops**, 6,938 Windows
processed frames, no fallback, and three successful pause/resume cycles. Maximum
sampled A/V error was 0.000408 s. Source transfer and FP16 color settings were
unchanged; a same-frame comparison had mean difference 0.024 on an 8-bit scale.

HLG/PQ exact-seek, rapid drag, final-frame and return/resume checks passed.
HLG 75 s paused seek took 0.398 s. F1 P7 MEL representative playback, pause/resume,
seek, chapter change and menu navigation each had 0 output/decoder drops and no
fallback. The native disc menu was visually checked: right navigation moved selection
from Play to Scene Selections. The main feature retained 16 chapters; this was a bounded F1 regression,
not a whole-film playback test.

Reply DCP previously had 3 startup output drops. Two later startup diagnostics
had none; the decoded queue filled before steady output. No speculative decoder
warmup change was made. The subsequent complete 274.708 s Reply DCP run had 0 output/decoder drops,
three successful pause/resume cycles, no Windows-DV filter, and maximum sampled
A/V error 0.000169 s. Source PCM sound pairing and display ICM remained active.
The earlier 3 startup drops were not reproduced in these checks; this is a
current-candidate test result, not attribution to a new DCP decoder change.

The signed source checkpoint is committed first. Local replacement follows the remaining visual/source acceptance checks, with
a backup of the prior runtime. No installer or GitHub
Release is published by this source update.

## Source and checks

`core/mpv`, `windows/native-dv`, `src/common/plugins/mpv-dcp`, and the libplacebo
patch are the maintained sources. Build dependencies and commands are in
`packaging/windows/README.md`; the new dependency pin is in `sources.lock.json`.
No proprietary DLL, KEYDB, device ICM, credentials or test movie is committed.

Run Lua tests with LuaJIT, passing the corresponding script/repository path.
`tests/test_dv_seek_preroll.py <output-dir>` compiles the actual pre-roll helper with
GCC into the requested build directory. The native-controls and SDR-color tests
cover preserved dependency gating, FP16 status, DCP exclusion and display-mode
behavior. The generic DCP test uses an unknown CPL, not a movie-specific fixture.

`tests/test_dv_prefetch.py <output-dir>` compiles the real filter `process()`
against pin stubs and checks capacity/EOF boundaries. In a Visual Studio x64
Native Tools shell, `tests/test_windows_gpu_fences.py <output-dir>` tests 64 queued
GPU round trips with exact UNORM/FP16 pixels and the legacy synchronization branch.
The GPU fixture requires a compatible hardware adapter; its bounded readback is
for verification only, not the playback path.
