# GPU-next DV controls and source color diagnostics

The HDR menu and `i` panel separate the original frame transfer from the effective renderer input. `gamma-original` and `primaries-original` expose the pre-DV-mapping values already stored by mpv; they do not change pixel processing.

## Why HLG can become PQ

For ordinary HLG, FFmpeg ARIB STD-B67 maps to libplacebo HLG, whose shader implements inverse OETF and OOTF. For Dolby Vision with RPU, libplacebo maps the reconstructed signal to BT.2020/PQ. This is not permission to relabel raw HLG samples as PQ: the DV reshaping and color transform must actually run. Windows native-SDR processing instead sends negotiated SDR/sRGB to gpu-next. Neither output should be forced back to HLG.

Tone mapping algorithms such as spline and bt.2390 are a different choice from the input transfer function. The existing accepted Windows P5/P8 SDR and P7 paths, ICM ownership, target-auto settings and libplacebo tone/gamut defaults are unchanged.

Libplacebo prefers 16-bit floating-point intermediate FBOs. A 10-bit display surface does not imply 10-bit intermediate calculations. The local runtime log confirms an rgba16hf intermediate texture. No Dolby-bridge pixel conversion was copied into libplacebo.

## Main toolbar EL button

`button:dv_el` is beside `button:hdr_dv`; it is hidden for single-layer media. Detection uses the primary track's declared EL or dependent sibling, not the public track list alone: mpv can hide the virtual EL track from that list. `dolby-vision-el-present` reports that structure, not successful pixel reconstruction.

The button controls **EL image contribution**, using `format=enhancement-layer=no`. It retains the splitter, EL decoder and RPU inheritance. It does not save decoding work, strip RPU, rewrite a profile, crop the movie or modify media.

- Starting on gpu-next: off/on remains gpu-next; Windows is never enabled.
- Starting on Windows: retain the accepted full-EL route. The EL comparison button is visible but inactive, with a tooltip to select gpu-next first. The button never changes backend. Automatic Windows/libplacebo round trips were rejected because transient unpaired startup frames appeared after reinsertion; that experimental behavior is not shipped.
- Failed changes report an error. On the next file, EL is restored rather than leaking a comparison setting.

A three-second top-right message identifies actual Windows processing or DV-tagged gpu-next rendering. It is this player's status message, not an official Dolby certification logo. Reduced EL-off output is explicitly labeled.

## Open-path color criteria

The open renderer uses source color tags, available RPU picture-mapping coefficients and matrices, and Level 1 scene values. BL reshaping precedes FEL residual composition and the DV color transform. FEL residual composition requires a paired enhancement-layer frame and active nontrivial NLQ. Display tone and gamut mapping follow reconstruction.

Advanced uses a staged FP32 path for P8.4 by default and a low-overhead 32×18 probe for the other eligible inputs. Gamut mapping defaults to perceptual compression into BT.709; relative, desaturate and darken are available for comparison. The [Advanced Dolby Vision SDR Rendering guide](advanced-dv-hdr-sdr.md) describes the Windows SDR-only boundary and the mutually exclusive device direct, system ICC and 3D LUT modes. The optional adaptive highlight refinement is for direct SDR output; it is not layered over the ICC or LUT path. OEM ICC tags such as `MHC2` and `DVB1` are not an implementation of Dolby's licensed viewing modes.

References: https://ffmpeg.org/ffmpeg-bitstream-filters.html#dovi_005fsplit ; https://github.com/haasn/libplacebo/blob/master/src/shaders/colorspace.c ; https://github.com/mpv-player/mpv/blob/master/demux/dovi_split.c

## Bounded checks

The real 4K60 P8.4 sample, with Windows disabled, exposed original HLG and effective DV/PQ. A separate base-only check exposed actual HLG at the renderer. Both bounded runs had zero decoder/output drops. BL_EL off/on on gpu-next remained on libplacebo, with visible removal/restoration of the woman in the test image and zero decoder/output drops in the successful run. These are interaction checks, not full-film playback or calibrated color acceptance. Runtime packaging must replace the complete player/config/helper directory together; replacing only `mpv.exe` leaves the new controls and route policy absent.

The top-right label is an application notification, not a claim to Windows native DV HDR. P5/P8 native-SDR color and ICM code is unchanged apart from guarding against Windows enable while EL comparison is disabled.
