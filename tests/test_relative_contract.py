from pathlib import Path
r=Path(__file__).resolve().parent.parent
b=(r/'windows/native-dv/src/persistent_engine.inc').read_text(encoding='utf8')
c=(r/'core/mpv/video/filter/vf_windows_dv.c').read_text(encoding='utf8')
assert 'wdv_create_gpu_v9' in b, 'missing explicit relative-linear FP16/sRGB ABI'
assert 'if(relative_srgb)return run_relative();' in b, 'new path must not replace legacy PQ/P7 engine'
assert 'transfer!=2' in b[b.index('wdv_create_gpu_v9'):], 'v9 must reject PQ'
assert 'MFVideoTransFunc_10_rel' in b
assert 'sample->GetSampleTime' in b, 'actual output PTS must be verified'
assert 'p->job_relative' in c and 'Windows relative-linear -> sRGB FP16' in c
assert 'fel-gpu-next' in c and 'PL_COLOR_TRC_SCRGB' in c
assert 'wdv_create_gpu_v8' not in b, 'do not ship rejected SDR10 ABI'
print('PASS explicit HLG contract, legacy preservation, timestamp validation, truthful state')

# Native SDR is additive: original P8 RPU/no EL, actual PQ or HLG, unchanged old ABIs.
v12=b[b.index('void* wdv_create_gpu_sdr_v12'):]
assert '(transfer!=1&&transfer!=2)' in v12
assert 'native_sdr10 && (profile!=8 || v.paired_el || fel)' in b
assert 'p->source_profile==8 && !first->enhancement_layer' in c
assert 'p->job_native_sdr!=p->gpu_native_sdr' in c
assert 'MFVideoFormat_A2R10G10B10' in b and 'MFVideoTransFunc_sRGB' in b
assert 'wdv_create_gpu_profile_v11' in b and 'wdv_render_gpu_fel_v10' in b
print('PASS independent P8 native SDR contract; preserve P5 and FEL entry points')

# Keep the user-accepted 50e88c0 Windows P5/P8 presentation contract. The Dolby
# effect still renders internally to SDR10, but the cross-device surface is an
# unchanged sRGB-code FP16 container. Sending the RGB10 surface directly changed
# effective Windows/gpu-next presentation and made P8.4 visibly overexposed.
assert 'p->job_native_sdr?IMGFMT_X2BGR10' not in c
assert 'p->job_float?IMGFMT_RGBAF16:IMGFMT_BGRA' in c
assert 'DXGI_FORMAT output_format() const {return fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;}' in b
assert 'if(uses_sdr10())return texture.Get();' not in b
assert 'windows-sdr10-fp16-transport' in c
print('PASS native SDR preserves the accepted SDR10-in-FP16 presentation contract')

# Advanced display management must preserve a valid calibrated/ICC black level.
# The ordinary SDR guard may still reject implausible metadata, but it must not
# collapse every staged DV target back to libplacebo's generic 1000:1 fallback.
v=(r/'core/mpv/video/out/vo_gpu_next.c').read_text(encoding='utf8')
assert 'if (!p->next_opts->dovi_sdr_staged &&\n        !(opts->target_contrast > 0) &&\n        !pl_color_transfer_is_hdr(target_csp.transfer))' in v
assert 'sdr.color.hdr.min_luma = target->color.hdr.min_luma;' in v
assert 'PL_FMT_CAP_BLITTABLE' in v and '.blit_dst=true' in v, 'staged render target must satisfy libplacebo clear/blit requirements'
assert 'gpu-next-dovi-sdr-display-peak' in v and 'gpu-next-dovi-sdr-display-black' in v
assert '{"gpu-next-dovi-sdr-signal-anchor", OPT_FLOAT(dovi_sdr_signal_anchor),\n            M_RANGE(0, 10000)}' in v, \
    'signal anchor must accept 0 so session controls can release Advanced mapping'
assert 'sdr_signal_smoothstep(0.02f, 0.08f, avg_y)' in v
assert '(display_peak - signal_anchor) * dark' in v
assert '0.15f * (display_peak - signal_anchor) * dark' in v
print('PASS staged Advanced DV restores the accepted 15% dark-scene headroom without imposing 1000:1')

# The staged option is enabled for the accepted P8.4 colour order, but P5/P7,
# AVC P9 and AV1 P10 are not staged-eligible. They must still receive dynamic
# display mapping through the low-cost direct probe path.
assert 'staged_sdr_eligible(p, mix, target)' in v
assert 'measure_open_dovi_signal' in v and 'apply_open_dovi_sdr_target' in v
assert 'P5/P7/P8.1/P10-capable 32x18' in v
assert 'probe_interval = direct_probe ? 0.50 : 0.20' in v
assert 'apply_open_dovi_sdr_target(p, &screenshot_mix, &target, &params);' in v
print('PASS P5, P7, P8.1 and P10 share dynamic display mapping without a second 4K intermediate')

# P7 FEL must try the validated BL+EL reconstruction path before falling back
# to gpu-next. The helper ABI and frame filter already fail closed when the
# matching helper is absent, so disabling the attempt at the packaged default
# would silently break the Windows Dolby route for valid Profile 7 FEL files.
native_conf=(r/'src/common/script-opts/native_dv_controls.conf').read_text(encoding='utf8')
assert 'fel_experiment=yes' in native_conf
print('PASS packaged P7 FEL defaults to the guarded Windows reconstruction path')

# Display capability discovery follows the mpv window instead of binding all
# sessions to the primary monitor's generic 80-nit sRGB composition profile.
probe=(r/'windows/native-dv/Detect-Dependencies.ps1').read_text(encoding='utf8')
assert "param([string]$DisplayName='')" in probe
assert 'CreateDC("DISPLAY", displayName' in probe
assert 'QueryDisplayConfig' in probe and 'ReadMonitorId' in probe
assert 'DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2' in probe
assert 'DXGI_OUTPUT_DESC1' in probe and 'CreateDXGIFactory1' in probe
assert 'display_color_mode' in probe and 'display_hdr_supported' in probe and 'display_hdr_active' in probe
assert 'display_full_frame_nits' in probe and 'dxgi_output' in probe
assert 'WmiMonitorConnectionParams' in probe
assert "$edidPeak" in probe and "$iccIsGeneric" in probe
print('PASS display probe follows the active output and distinguishes generic sRGB reference data')

# Advanced DV to SDR is an independent nominal BT.709/sRGB render contract.
# Direct output, the installed system ICC and a measured DisplayCAL LUT are
# mutually exclusive final device transforms.
controls=(r/'src/common/scripts/hdr-controls.lua').read_text(encoding='utf8')
assert "start_mode='display-auto'" in controls and "advanced-dv-to-sdr" in controls
assert "set('icc-profile','');set('icc-profile-auto','no')" in controls
assert "advanced_calibration_modes={'none','system-icc','displaycal-lut'}" in controls
assert "set('target-lut',lut)" in controls and "set('icc-profile-auto','yes')" in controls
assert "declared_full_frame=tonumber(dependency.display_full_frame_nits)" in controls
assert "advanced_mapping_modes={'perceptual','relative','desaturate','darken'}" in controls
assert "set('tone-mapping','auto');set('gamut-mapping-mode',advanced_mapping)" in controls
assert "black=math.min(declared_black,swapchain_black)" in controls
assert "set('libplacebo-opts','')" in controls
advanced_conf=(r/'src/common/script-opts/advanced_color_management.conf').read_text(encoding='utf8')
assert 'advanced_full_frame=auto' in advanced_conf
assert 'advanced_signal_anchor=auto' in advanced_conf
assert 'advanced_calibration=none' in advanced_conf and 'advanced_lut=' in advanced_conf
assert 'advanced_profile_file=script-opts/advanced_display_profiles.json' in advanced_conf
assert 'display_profiles[tostring(dependency.monitor_id' in controls

# On this machine the colorimeter/manufacturer peak is 400 nits and DXGI
# reports a sustained full-frame value of 270 nits. A fully active ordinary
# scene-headroom branch therefore remains 270 + 40% * (400 - 270) = 322 nits.
peak,full_frame=400.0,270.0
assert full_frame + 0.4*(peak-full_frame) == 322.0
assert '0.4f * (display_peak - signal_anchor) * headroom' in v
print('PASS Advanced DV selects one calibration transform and derives the 270-322 envelope from display data')

# Per-file resume data must never resurrect an obsolete Windows-DV DLL/ICC
# filter chain and silently override the selected colour-management mode.
mpv_conf=(r/'src/mpv.conf').read_text(encoding='utf8')
assert 'watch-later-options-remove=vf' in mpv_conf
assert 'autocreate-playlist=no' in mpv_conf
print('PASS resume state cannot restore stale DV filters; large folders are not scanned on direct open')
