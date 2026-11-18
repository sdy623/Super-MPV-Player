# Native SDR profile verification — 2026-11-17

P5 native SDR and P7 FEL reconstruction were introduced in commit `075526f`; this follow-up preserves them and adds P8.1/P8.4 native SDR plus output-contract-based ICM restoration.

## Accepted output and input contracts

| Path | Input and processing | Output / MPV ICM |
| --- | --- | --- |
| P5 | Independent dvhe.05; raw full-range P010, unchanged P5 RPU | Dolby SDR10 sRGB/709, unchanged-code FP16 transport; extra MPV ICC off |
| P8.1 | Original limited-range BT.2020 NCL/PQ and P8 RPU, no EL | Same native SDR10 output; explicit sRGB on renderer input/output |
| P8.4 | Original limited-range BT.2020 NCL/HLG and P8 RPU, no EL | Same native SDR10 output; explicit sRGB on renderer input/output |
| P7 FEL | Actual synchronized BL+EL GPU reconstruction, quantized P010 and adapted RPU to existing Windows path | Existing legacy SDR path and its ICC handling retained; guarded default attempt, not 12-bit lossless |

Native SDR direct presentation clears MPV's explicit target ICC and disables auto-loading only while this output contract is active. On exit it restores saved settings. Original mapping and DCP retain their color management. Target brightness is auto; no manual gamut/brightness compensation was introduced. Repository default remains display-auto. FEL now attempts the tested helper by default and fails closed to the original gpu-next BL/EL path when that helper is absent or incompatible.

The user confirmed normal P5 brightness/appearance and normal P8.4 color/brightness compared with Vivid on the internal SDR display. P8.1 has runtime output evidence but no separate user visual acceptance. The output class matches Vivid's reported SDR10/sRGB/709. The log and monitor binding do not prove a vendor-internal factory ICC LUT or calibrated display accuracy.

## Decisive evidence

- Exact 27-second P8.4 output: original RPU versus a frozen valid first RPU changed 101,728 of 110,592 sampled native SDR RGB components; mean absolute normalized difference 0.03634538. Metadata changes output pixels, not just counters.
- The accepted SDR10-to-FP16 transport had maximum sampled normalized error 0.000487327 and zero round-trip 10-bit mismatches. A later direct-RGB10 optimization changed effective P8.4 presentation and was withdrawn; the FP16 container is retained for the validated brightness contract.
- Full 116.5-second 4K60 P8.4 playback: EOF, no decoder drops or fallback, two pause/resumes, maximum sampled absolute A/V error 0.000081 seconds. Six output drops occurred during startup; no later increase. This is not a zero-drop claim.
- Candidate launcher P8.1/P8.4 short checks: native SDR contract, no output/decode drops or fallback. F1 BD menu rendered, menu/down navigation accepted, timeline and paired-layer Windows processing retained.
- Installed-launcher checks: P8.4 584 processed frames, 3 output drops in a screenshot-bearing short run; P5 201 frames, zero drops; P7 FEL 197 frames, zero drops. All had no decoder drops or fallback. The actual nested executable and helper paths were checked, not merely the root executable.
- Actual native/original/native transitions restored ICC and transfer settings correctly. Lua regression tests cover P5/P8, legacy P7, DCP, original-mapping overrides and auto brightness.

The first installation check found the old GUI launcher selecting an outdated nested core and rejecting fel-experiment. Updating both effective entry points resolved that local installation mismatch. Deployment must keep the selected executable, bridge and helpers consistent. Do not replace only a root executable when a retained launcher selects a subdirectory.

No media, proprietary Dolby binary, diagnostic DLL, local screenshot or personal file path is included in this source commit. Local runtime was backed up and replaced; this source update does not publish a release or rebuild an installer.
