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

## Not yet accepted for installation

The subsequent complete116.512s HLG run, with normal window controls and three
pauses, had8 output drops (0 decoder drops, no fallback, sampled A/V error<=0.000021s).
Drops occurred around pause/resume. Reply DCP complete playback with the queue
had3 startup output drops. Earlier failed/intermittent runs are retained in the
local work record rather than replaced by shorter passing tests.

**The user requested committing first and fixing these drops before replacing
the local installation.** No new installer or GitHub Release is published by
this commit. This is not a zero-drop whole-player acceptance claim.

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
