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

## Color management
Ordinary SDR DCP uses 12-bit XYZ/ST428 and floating-point gpu-next processing.
Default `white_mode=preserve` keeps XYZ colorimetry, using a wide D65 RGB working
space before final display gamut mapping and factory ICM. This does not declare
the original master BT2020 and does not clamp it into Rec709. No CPL or title
exceptions are read or shipped. The matched mpv/libplacebo XYZ patch is required.

While playing DCP, Ctrl+Alt+H (or the theme's DCP/palette button) opens:
- Preserve XYZ colorimetry (default).
- Cinema reference-white adaptation (DCI to display), an intentional appearance
  adjustment, not automatic source-white detection or projector calibration.
Both preserve precision and display ICM. Selection is session-local. To change
the startup default, set `white_mode=cinema` or `preserve` in script-opts/dcp.conf.

The Windows Dolby filter is excluded file-locally. Subsequent non-DCP playback
restores the prior filter/settings. PCM pairing, chapters and the timeline are
unchanged. DCP HDR/3D, encrypted/KDM assets and DCP timed-text import are not
implemented by this SDR path; see the limitations above.

Standalone XYZ/ST428 MXF uses the same color policy and menu; a picture-only MXF
has no CPL sound pairing. JPEG2000 playback has a file-local decode queue bounded
by 8 frames, 0.35 seconds and 256 MiB. This separates software decoding from the
player thread without reducing resolution or precision. Ordinary video retains
its existing queue options.
