# DCP folder / CPL playback

Drag a complete, unencrypted 2D DCP folder, ASSETMAP, or its CPL XML into the existing player. A single CPL is opened automatically. For a package containing several CPLs, drag the desired CPL XML explicitly. Picture and sound are resolved by asset UUID, not filename similarity.

The importer uses mpv's native EDL timeline, with parallel picture/audio tracks and CPL EntryPoint, Duration and EditRate for each Reel. It neither transcodes nor writes into the DCP. The existing native window, uosc theme, audio output and seek bar remain in use. Reel boundaries become chapters. No separate player or local streaming server is involved.

For SMPTE MainSoundConfiguration layouts mono, stereo, 5.1 and 7.1, the file-local index-based pan maps the declared speakers and omits only channels marked `-`. The Reply test DCP declares `51/L,R,C,LFE,Ls,Rs,-,-`, not Atmos. Output is PCM through the user's existing Windows output setting.

## Required core fix

This build adds `--ad-lavc-dcp-pcm-indexed`, default **no**. Only the importer enables it, locally, alongside an explicit CPL pan. For PCM s24le with a custom FFmpeg layout it assigns a count-preserving representable decoder layout. It does not rearrange sample bytes. The subsequent pan applies the actual speaker assignments from CPL. The intermediate eight-channel decoder label can read `7.1`; that is not a claim that the DCP is 7.1. The final mapped input is 5.1 for the Reply sample. Existing non-DCP decode is unchanged.

The Python helper uses the bundled runtime, reads only package XML and asset existence, and returns an in-memory EDL. No media cache or generated file is stored in the source directory. Asset paths outside the package, URI protocols, XML entities, incomplete assets, encrypted tracks, mismatched durations and partial missing-audio reels are rejected. Errors do not fall through to disconnected MXF playback.

## Current limits

Self-contained, single-volume unencrypted 2D SMPTE/Interop picture + sound compositions. Encrypted/KDM, stereo 3D, split/chunked assets and external VF/OV resolution are not implemented. DCP XML/MXF subtitles and closed captions are not imported and produce a visible warning. DCP is not Blu-ray; it does not supply a Blu-ray disc menu. Existing BD playback is separate and unchanged.

## Files

- `scripts/mpv-dcp.lua`: installed loader.
- `plugins/mpv-dcp/main.lua`: local on_load hook, pairing, file-local options, chapters.
- `plugins/mpv-dcp/dcp.py`: read-only XML/EDL parser.
- `core-dcp.patch` in the task workspace: narrowly scoped PCM decoder opt-in.

Implementation follows the bundled upstream `DOCS/edl-mpv.rst` and hook/property manual. The unchanged gpu-next renderer recognizes the sample's JPEG2000 xyz12 / XYZ / ST428 signal. No Dolby profile is forged or forced on DCP content.
