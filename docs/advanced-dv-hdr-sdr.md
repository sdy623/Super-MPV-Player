# Advanced Dolby Vision SDR Rendering (gpu-next)

The open renderer in Super-MPV-Player maps Dolby Vision video to the current SDR screen. Its optional highlight refinement uses both display headroom and the brightness distribution in the image. An eligible shot can gain highlight separation while large white fields and small highlights in dark scenes keep their balance.

The open rendering path uses FFmpeg, mpv gpu-next and libplacebo. It uses available Dolby Vision picture metadata (RPU), then tone-maps and gamut-maps the image to an SDR output. It needs no Dolby extension and sends no native Dolby Vision signal to the display. The [optional Windows Dolby renderer](open-edition.md#windows-dependency) is a separate path and may produce a different image.

## Find out whether your display can use it

This mode is intended for Windows laptop panels, desktop monitors or other displays that Windows identifies as **SDR-only**. It is most useful when the screen has headroom between sustained large-area white and small-window white, low enough blacks, and a stable picture mode. A more modest SDR screen can still use the base mapping; it may see little or no extra highlight lift.

- The active display must be reported as SDR or wide-gamut SDR **and** HDR-unsupported. Turning Windows HDR off on an HDR-capable monitor does not make it eligible. Unknown, HDR-capable and HDR-active outputs stay on normal gpu-next negotiation. Which screen owns the player window matters.
- The input must be recognized as Dolby Vision on the open gpu-next path. Ordinary SDR, HDR10, HLG and the RPU-off base-layer comparison do not get the Advanced DV curve. HDR10 or HLG present as compatible Dolby Vision base layers is a different case.
- The open route recognizes P5, P7 MEL/FEL, P8.1, P8.4, P9 and P10; recognition is not per-profile calibration or validation. P5 needs valid Dolby Vision metadata; it does not have an ordinary HDR10 fallback. A Profile 7 label does not establish FEL reconstruction: the picture contribution requires an enhancement-layer frame paired to the right time and active residual parameters. Missing layers, invalid metadata or a seek without enough pre-roll can change what is rendered.

Peak luminance, sustained white, black level and Windows' SDR reference white are different quantities. An EDID/DXGI or factory ICC declaration is useful for an initial target, but it is not a meter reading. A bright software target neither raises backlight output nor gives a display another display's contrast or EOTF.

## Start with a same-display A/B

1. Play a Dolby Vision clip on the intended screen. Keep the Windows output, panel picture preset and backlight fixed. Use **Ctrl+Alt+H** to open the HDR menu, then choose **Advanced DV to SDR (独立动态映射)**. The menu selection switches away from optional Windows Dolby processing.
2. Start with automatic display parameters. Unmatched displays default to perceptual gamut mapping; the BOE0CFB inner panel uses a dedicated darken preset. Its 616 / 0.203 / 270 values are software parameters (peak / black / signal anchor), not physical measurements. An explicit gamut setting overrides the panel preset. Use **device SDR direct output (设备 SDR 直出)** on a screen already in a suitable sRGB/Rec.709 mode. On a wide-gamut screen that needs device correction, try **system-installed ICC (系统安装 ICC)** instead; direct output may look too saturated there.
3. To test the extra highlight refinement, set the following in `portable_config/script-opts/advanced_color_management.conf`, restart the player, and select Advanced again. It is **off by default** and requires direct full-range BT.709/sRGB RGB output, without mpv ICC, a target LUT or a competing output shader.

   ```ini
   advanced_adaptive_highlights=yes
   ```

   In an already configured Advanced SDR test setup operated directly through mpv, the underlying renderer option is `--gpu-next-dovi-sdr-adaptive=yes`. It does not by itself select Advanced or determine a display target.
4. Compare with `advanced_adaptive_highlights=no` in Advanced on the **same frame**, then play through the surrounding cuts. Check a scene with textured highlights, one with a large white area, one with a dark background and a small bright object, and a natural face. Look for lost texture, lifted blacks, clipping, color changes and brightness jumps during cuts. Check the path in the HDR menu; choosing the mode alone is not evidence of an active Advanced target.

To select Advanced as your startup mapping mode, set `start_mode=advanced-dv-to-sdr` in the same file. If the optional Windows Dolby renderer takes over, use the menu to select the open path explicitly. Return to the display-following baseline with the menu item **跟随显示器：自动 SDR / HDR10** or set `start_mode=display-auto`. Leave the refinement off if you prefer the base mapping.

## Why the extra refinement is not a LUT

The main mapping uses the display's peak, black level and sustained brightness anchor together with source and scene information. When opted in on a supported direct SDR output, the refinement looks at average brightness, the population of upper tones and the amount of near white. It adds a bounded lift where there is useful signal and display headroom, reduces or skips the lift on broadly bright frames, and avoids an extra highlight lift in very dark scenes. It leaves digital black and peak white endpoints unchanged; it does not remove existing clipping or change the panel's black floor. For paired FEL content, it can respond to strongly colored highlights without treating neutral white text as the same signal.

In matched P8.4 and reconstructed P7 FEL development frames, the refinement added lift in the eligible shots, did not lift a broad white field or a dark scene's small highlights, and left the tested FEL white text and black border unchanged. These are software signal comparisons; see the [pre-release notes](release-2.2.0.md) for build-specific validation and limits.

## Expert option: use a 3D LUT for display calibration

Use a `.cube` here to correct a **particular display in a particular state**, not to re-tone-map every Dolby Vision source. gpu-next has already done the content-dependent HDR-to-SDR work. The output LUT receives normalized RGB after encoding into the target color space (in this path, BT.709/sRGB). A PQ-input, HLG-input, legal-range or creative HDR conversion LUT will interpret those values differently. See the [mpv target-LUT contract](https://mpv.io/manual/master/#options-target-lut).

### Measure, build and verify

1. **Fix the display state.** Warm up the panel at the SDR mode and brightness you will actually use. Record the monitor, backlight, preset, refresh rate, Windows color-management state and which hardware/GPU one-dimensional calibration is active. Avoid adaptive brightness, night color shifts and overlays during measurement. Use the appropriate meter mode or correction for the display.
2. **Measure the brightness envelope and characterize color.** With a colorimeter, measure peak white on a small window (for example, 10% of the screen) and sustained full-screen white separately, recording background and duration. Measure full-screen and in-picture black, and a fixed-background neutral gray ramp for the actual EOTF (light output versus input code). Retain absolute readings in cd/m² and record any result below the meter's useful range rather than entering zero. Measure white point and color patches to build a display characterization profile. A few white/black readings alone cannot create a 3D LUT.
3. **Build a device transform.** In a tool such as [DisplayCAL](https://displaycal.net/) (see “Creating 3D LUTs”), use a source profile with BT.709/sRGB primaries and the actual sRGB transfer, and the measured profile for the destination display. Export a compatible normalized full-range RGB `.cube`. Keep the measured calibration state consistent: if GPU/hardware 1D calibration is already loaded and characterized, do not also bake the same correction into the 3D LUT. Do not choose a madVR/video-range/PQ conversion preset on the basis of the source file's HDR tag.
4. **Verify the applied path.** Measure an independent gray and color verification set through the actual player/output configuration, not just through the profiler's patch window. Compare neutral tracking, shadows, white point, color error and clipping with the LUT off and on. Save the configuration alongside the result. Changing monitors, panel presets, brightness or Windows color-management state can invalidate the LUT; verify again rather than reusing the old transform blindly.

For stable automatic behavior across displays, put measurements and an optional LUT in `portable_config/script-opts/advanced_display_profiles.json` keyed by Windows monitor ID. Adapt the [example profile](../src/common/script-opts/advanced_display_profiles.example.json): its numbers illustrate the file format, not recommended brightness settings. The peak and full-frame values must come from the same mode; do not set a manual signal anchor just to make a sample brighter.

### Connect an existing compatible LUT

Place your LUT at `portable_config/luts/display-sdr.cube` (create the directory if needed), then edit `portable_config/script-opts/advanced_color_management.conf`:

```ini
advanced_calibration=displaycal-lut
advanced_lut=luts/display-sdr.cube
```

Restart and select **Advanced DV to SDR**. In the same HDR menu, select **高级：DisplayCAL 3D LUT** under **Advanced 色彩管理** to switch calibration modes in the current session. An empty `advanced_lut` lets the per-monitor profile supply the file instead. Do not set one global path when several displays require different LUTs.

The player assigns that file as the sole output target LUT and disables its own ICC transform. The sampled highlight refinement is bypassed when the LUT is active; the base Advanced tone/gamut mapping remains. This player-side exclusivity does not disable independent system or driver calibration. A missing file falls back to device direct output; verify the file and effective calibration before judging color. To undo, select **system-installed ICC** or **device SDR direct output** and update the config if you want that choice on restart.

**TODO — reproducible DisplayCAL tutorial.** Publish a versioned Windows walk-through with the pattern path, meter/patch settings, exact export selections and an independent in-player verification report. Validate the source/target transfer, full-range neutral and color endpoints, one-time calibration ownership, and monitor switching before recommending a preset. The connection above is documented; an end-to-end measured export preset and example LUT are not supplied.
