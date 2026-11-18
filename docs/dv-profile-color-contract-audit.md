# Pre-v12 Dolby profile / output / ICM audit — 2026-11-17

This is the historical audit that motivated v12. Current P8.1/P8.4 native SDR implementation and accepted appearance are recorded in [native SDR verification](native-sdr-profile-verification.md). The route descriptions below refer to the pre-v12 candidate.

## Decision
The bitstream profile constrains the encoded signal, layer arrangement and metadata. It does not select one universal PC output format or mandate an MPV ICC switch. Input interpretation, Dolby display management, output encoding and display color conversion are separate contracts. The output contract and the actual display color-management owner must control ICC policy, not profile ID alone.

The current P5-only ICC-off setting remains the user-accepted appearance baseline. It is NOT a standards-derived general P5 rule. The audit changed no player settings or binaries. This subsequent source checkpoint preserves the experimental implementation; no default player was replaced.

## Input contracts
| Profile | Input to correct DV processing | Current candidate audit |
| --- | --- | --- |
| P5 | HEVC 10-bit full-range IPT-PQ-C2, BL+RPU, no EL. Original unspecified VUI is valid; newer explicit signaling also exists. Do not interpret as ordinary BT.2020 NCL PQ. | Independent dvhe.05 entry; original P5 RPU; raw P010 full-range; unknown VUI matched to Windows decoder for the tested reference. Gate currently accepts only center-left chroma: tested subset, not proof of every legal P5 stream. |
| P7 MEL | HEVC dual layer, HDR10-compatible BT.2020 NCL/PQ BL; synchronized BL/EL/RPU. | Existing paired path retained, adapted to the component's P8 entry. This is not advertised native P7 support. Output remains legacy BGRA; color handling is different from P5/P8.4. |
| P7 FEL | Same BL plus actual enhancement residual reconstruction. | Experimental GPU BL+EL reconstruction -> 10-bit P010 PQ -> adapted RPU -> Windows. Enhancement picture contribution is retained; this handoff is not 12-bit lossless. No new playback certification this audit. |
| P8.1 | HEVC 10-bit limited-range BT.2020 NCL/PQ, BL+RPU, no EL. | PQ branch; gpu-float only selected for HLG/P5, so current P8.1 follows legacy BGRA path. |
| P8.4 | HEVC 10-bit limited-range BT.2020 NCL/HLG, BL+RPU, no EL. Read actual chroma location and any alternative-transfer signaling. | Original HLG explicitly mapped to MFVideoTransFunc_HLG, not PQ. Direct dvhe.08 effect; output relative-linear FP16 -> one sRGB encoding -> MPV. Not the output negotiated by Vivid on this screen. |
| P9 | AVC 8-bit, SDR-compatible BT.709 base + DV metadata, no EL. SDR-compatible input does not constrain all eventual DV outputs to SDR. | This candidate has no dvav.09 entry; non-PQ/non-HLG input rejected. Do not advertise working Windows P9 here. |

P8.2 has no separately validated path here. Current Dolby public table lists P8 compatibility IDs 1/4; legacy P8.2 must be treated as a separate implementation/support question, not routed into PQ by guessing. P10/20 are outside this candidate's implemented scope.

## Output paths actually present in code
1. P5: MFT A2R10G10B10 + sRGB + BT709 + full -> normalized copy to FP16 (no second OETF) -> MPV sRGB source, no source ICC; target ICC disabled by P5-only Lua policy.
2. P8.4: MFT A16B16G16R16F + MFVideoTransFunc_10_rel + BT709 + full -> sRGB OETF once -> FP16 sRGB transport -> MPV sRGB source, no source ICC; target ICC enabled by default.
3. P7/P8.1: MediaEngine BGRA SDR -> MPV attaches factory ICC as SOURCE profile and loads display ICC as TARGET. Source attachment describes pixels, it does not itself convert them. Its device-RGB assumption needs independent output evidence; having the same source/target ICC can approximately cancel a conversion but cannot establish the assumption.
4. The normal gpu-next path retains source DV/HDR processing and performs its own mapping; Windows-processed frames clear source DV/HDR side data to avoid repeating source DV processing.

Source locations (relative to profile-routing-v11):
- bridge/src/persistent_engine.inc:171-179 output surface/encoding; 232-242 input MF attributes; 259-280 monitor binding and negotiated output; 400-406 RPU dispatch.
- core/vf_windows_dv.c:196-216 output color and source ICC; 241-258 profile/transfer routing; 286-311 guards; 430-443 metadata.
- p5-sdr10-candidate/runtime/portable_config/scripts/hdr-controls.lua:94-151 target ICC and mapping policy.
- In the archived mpv source, `video/out/vo_gpu_next.c:1256–1268, 1539–1554` shows ICC also participates in target color and luminance hints; it is not a brightness slider.

## Historical test display and Vivid evidence
The read-only DisplayConfig queries (`display-state.txt`) reported an SDR output at 10 bpc. Windows reported Advanced Color inactive and HDR/WCG unsupported and inactive. GDI reported the display's factory profile, and panel brightness readback was 100%. This capture did not establish that Windows ACM applied another ICC transform. No system color settings were changed.

Vivid's actual P8.4 session at04:35 records profile8 compatId4, hdr=0/acmWcg=0, display ACI270/0.5, and Dolby MFT output=boosted SDR sRGB-709(R10G10B10A2). It also sets panel brightness100%. P5 session04:45 records the same output class. These are log observations, not calibrated nits or proof of a separate ICC transform inside Vivid. The log does not establish that Vivid applies the factory ICC LUT.

P8.4 differs from Vivid BEFORE the MPV gamut selector: FP16 relative-linear output versus native SDR10 output. It also differs in downstream ICC handling. It is premature to blame only perceptual versus relative. P5's prior same-frame comparison already demonstrated different numerical output from these two MFT output contracts; reuse that result, do not pretend texture bit depth alone implies equivalent rendering.

The target player was no longer running when this audit queried it; no current IPC state was obtained. On-disk code and existing captured evidence were audited, without launching a new video.

## ICM rule
- Standard sRGB/BT709 RGB on legacy SDR desktop: apply source-to-display conversion once when accurate display reproduction requires it (normally factory ICC on this machine).
- Verified device-encoded RGB already mapped to this panel: do not reinterpret it as standard sRGB and apply the panel transform again. Describe source pixels accurately and avoid an extra display transform.
- Windows Advanced Color/ACM actually active: provide correctly tagged standard output and follow the Windows-managed route; do not hardcode an extra factory conversion. This condition is not active now.
- Absolute linear scRGB and display-relative linear FP16 are distinct; neither is 'a reason to enable ICC' by itself.

SetOutputDevice binds the target monitor; public API documentation alone does NOT guarantee that a particular Dolby output has had the factory ICC transform applied. Likewise the output subtype/BT709 label alone does not demonstrate vendor-internal adaptation. That missing boundary is the remaining P5/P8.4 color-accuracy question.

Read-only LittleCMS probe (icc-numeric-probe.json) transformed ideal sRGB code values to this factory ICC. Neutral0.5 became about(0.4895,0.4906,0.4905); white1.0 became(0.99855,1,1); saturated primaries changed substantially. Relative/perceptual/absolute ICC intents produced identical values for these eight probes. This does NOT measure the screen or replicate MPV's entire pipeline; it shows why 'ICC always lowers white substantially' and 'changing ICC intent necessarily fixes it' are unsupported.

## Mapping controls: user-facing decision
The set containing clip is gamut-mapping-mode, NOT ICC intent and not Dolby profile selection.
- auto: renderer chooses algorithm; not a guaranteed no-op.
- perceptual: soft gamut compression; preferred starting point for original gpu-next HDR/DV-to-SDR mapping.
- relative: colorimetric boundary clipping with white-point adaptation; useful alternative for original mapping, not a repair for wrong transfer/range.
- absolute: similar colorimetric clipping without white adaptation; not a Dolby 'absolute PQ' mode and not recommended as a brightness fix.
- clip: hard per-channel clipping; can destroy highlight colors, not a mapping-off switch.
ICC intent is separate:0 perceptual,1 relative(default),2 saturation,3 absolute. There is no ICC clip intent.

For the Windows Dolby path, the player must own and constrain downstream mapping according to the actual negotiated output. Keep target brightness auto and do not ask the user to choose a gamut trick to compensate for a wrong output contract. Merely setting tone/gamut=auto does not establish pass-through if target primaries/luminance/ICC differ.

## Acceptance and next correction
The preferred correction is an explicit output contract (standard-sRGB / verified-device-RGB / display-relative-linear / absolute-scRGB) plus one named display-conversion owner; no permanent table of P5=ICC off, P8.4=ICC on. For Vivid parity, first align the P8.4 negotiated output and final presentation semantics with its native SDR10 route while preserving original HLG/RPU and the independent P7/FEL path. Then isolate the remaining ICC difference; do not stack output-format, intent and brightness changes. Until that boundary is established, preserve user-accepted P5 appearance but do not call all profile color pipelines certified.

## Primary sources
- Dolby profiles: https://ott.dolby.com/OnDelKits/Dolby_Vision_Online_Delivery_Kit/v1/Documentation/Specs/Visio_Profiles/help_files/topics/c_dovi_profiles.html
- Dolby profile notes: https://ott.dolby.com/OnDelKits/Dolby_Vision_Online_Delivery_Kit/v1/Documentation/Specs/Visio_Profiles/help_files/topics/c_notes_profile_public.html
- Microsoft transfer enums: https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/ne-mfobjects-mfvideotransferfunction
- Output-device binding: https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-imfvideoprocessorcontrol3-setoutputdevice
- ICC/Advanced Color: https://learn.microsoft.com/en-us/windows/win32/wcs/advanced-color-icc-profiles
- MPV gamut and ICC controls: https://mpv.io/manual/master/#options-gamut-mapping-mode and https://mpv.io/manual/master/#options-icc-intent
