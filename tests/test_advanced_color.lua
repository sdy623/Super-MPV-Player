local target=arg[1]
local p={
 ['idle-active']=false,
 ['video-params']={gamma='hlg',colormatrix='dolbyvision'},
 ['video-target-params']={['max-luma']=80,['min-luma']=0.203},
 ['options/gpu-next-dovi-sdr-display-peak']=0,
 ['options/gpu-next-dovi-sdr-display-black']=0,
 ['options/gpu-next-dovi-sdr-signal-anchor']=0,
 ['options/gpu-next-dovi-sdr-adaptive']=false,
 ['options/target-lut']='',
 ['user-data/dv-playback']={profile=8,base_transfer='hlg'},
 ['user-data/windows-dv-inwindow']={windows_processed=false,source_dv=true,source_base_transfer='hlg',
  dependency={display_peak_nits=270,display_full_frame_nits=80,display_black_nits=0.5,
   display_contrast=540,display_target_source='dxgi-output-desc1',display_name='\\\\.\\DISPLAY1',monitor_id='DISPLAY\\TEST-CALIBRATED',
   display_color_mode='sdr',display_hdr_supported=false,display_hdr_active=false,
   dxgi_output={max_luminance_nits=270,max_full_frame_luminance_nits=270,min_luminance_nits=0.203,
    bits_per_color=10,color_space=0,source='dxgi-output-desc1'},
   icc_profile_data={white_point_xyz={x=0.95,y=1,z=1.09},colorants_xyz={red={x=.64,y=.33,z=.03}}}}},
}
local observers,callbacks,timers={},{},{}
mp={
 log=function()end,
 get_property_bool=function(k,d)if p[k]~=nil then return p[k] end return d end,
 get_property=function(k,d)if p[k]~=nil then return p[k] end return d end,
 get_property_native=function(k,d)if p[k]~=nil then return p[k] end return d end,
 set_property=function(k,v)p[k]=v;return true end,
 set_property_native=function(k,v)p[k]=v;return true end,
 set_property_bool=function(k,v)p[k]=v end,
 commandv=function()end,osd_message=function()end,
 msg={error=error,warn=function()end},
 register_script_message=function(k,f)callbacks[k]=f end,
 observe_property=function(k,_,f)observers[k]=f end,
 register_event=function()end,
 add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end}end,
}
package.preload['mp.utils']=function()return {
 format_json=function()return '{}' end,
 parse_json=function()return {profiles={['DISPLAY\\TEST-CALIBRATED']={peak_nits=400,full_frame_nits=270,source='colorimeter+manufacturer'}}} end,
 file_info=function(path)if path=='D:\\calibration\\displaycal.cube' then return {is_file=true} end end,
}end
package.preload['mp.options']=function()return {read_options=function(o)
 o.start_mode='advanced-dv-to-sdr'
 o.advanced_signal_anchor='auto'
 if os.getenv('TEST_ADAPTIVE_OPT_IN')=='yes' then o.advanced_adaptive_highlights='yes' end
 o.advanced_calibration='none'
 o.advanced_lut='D:\\calibration\\displaycal.cube'
 o.advanced_profile_file='tests/fixtures/advanced_display_profiles.json'
end}end
mp.find_config_file=function(path)return path end
package.loaded['mp.utils']=nil;package.loaded['mp.options']=nil

assert(loadfile(target))()
observers['video-params']();timers[#timers]()
local dependency=p['user-data/windows-dv-inwindow'].dependency
local fixture_monitor=dependency.monitor_id
dependency.monitor_id='DISPLAY\\BOE0CFB'
observers['video-target-params']();timers[#timers]()
assert(p['options/gamut-mapping-mode']=='darken' and
       tonumber(p['options/gpu-next-dovi-sdr-display-peak'])==616 and
       tonumber(p['options/gpu-next-dovi-sdr-display-black'])==0.203 and
       tonumber(p['options/gpu-next-dovi-sdr-signal-anchor'])==270 and
       p['options/icc-profile-auto']==false,
 'BOE preset applies only to matched SDR Advanced output')
assert(p['user-data/hdr-controls'].advanced_target.full_frame==80,
 'BOE anchor is not the DXGI full-frame report')
dependency.monitor_id='DISPLAY\\OTHER'
observers['video-target-params']();timers[#timers]()
assert(p['options/gamut-mapping-mode']=='perceptual' and
       tonumber(p['options/gpu-next-dovi-sdr-display-peak'])==270 and
       tonumber(p['options/gpu-next-dovi-sdr-signal-anchor'])==80,
 'another output stays on auto targets and default gamut mapping')
dependency.monitor_id=fixture_monitor
observers['video-target-params']();timers[#timers]()
local adaptive_requested=os.getenv('TEST_ADAPTIVE_OPT_IN')=='yes'
assert(p['options/gpu-next-dovi-sdr-adaptive']==adaptive_requested,
 'adaptive highlights require an explicit opt-in on the Advanced DV SDR route')
assert(p['options/gpu-next-dovi-sdr-staged']==true,'advanced color management must be the P8.4 open-route default')
assert(p['user-data/hdr-controls'].advanced_target.dynamic_mapping==true and
       p['user-data/hdr-controls'].advanced_target.dynamic_path=='staged-fp32',
 'P8.4 must report its staged dynamic display path')
assert(p['options/target-peak']=='auto','panel peak must not be hard-coded into target-peak')
assert(p['options/target-contrast']=='auto','abandoned fixed 1000:1 contrast must not return')
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-peak'])-400)<0.0001,
 '50e88c0 colour order must use the active display peak rather than a baked 400-nit target')
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-signal-anchor'])-270)<0.0001,
 'per-monitor full-frame measurement must override the 80-nit SDR composition value')
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-black'])-0.203)<0.0001,
 '50e88c0 colour order must use the least-lifting credible SDR/device black floor')
assert(p['options/icc-profile-auto']==false and p['options/icc-profile']=='',
 'Advanced DV to SDR must never apply a system ICC/ICM transform')
assert(p['options/gamut-mapping-mode']=='perceptual' and p['options/libplacebo-opts']=='',
 'wide-gamut DV must use explicit perceptual BT.709 compression rather than the old saturation-prone softclip override')
assert(p['user-data/hdr-controls'].advanced_target.display_name=='\\\\.\\DISPLAY1' and
       p['user-data/hdr-controls'].advanced_target.icc_profile_data.white_point_xyz.y==1,
 'diagnostics must retain the selected output and ICC white point/colourants')
assert(p['user-data/hdr-controls'].advanced_target.full_frame==270 and
       p['user-data/hdr-controls'].advanced_target.signal_anchor==270 and
       p['user-data/hdr-controls'].advanced_target.dxgi_output.max_full_frame_luminance_nits==270,
 'diagnostics must distinguish DXGI full-frame luminance from measured peak luminance')
assert(p['user-data/hdr-controls'].advanced_target.device_profile.source=='colorimeter+manufacturer',
 'the measured peak override must be selected by monitor id rather than applied to every display')
local saved_monitor=p['user-data/windows-dv-inwindow'].dependency.monitor_id
p['user-data/windows-dv-inwindow'].dependency.monitor_id='DISPLAY\\OTHER'
observers['video-target-params']();timers[#timers]()
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-peak'])-270)<0.0001,
 'a per-monitor colorimeter peak must not leak to another display')
p['user-data/windows-dv-inwindow'].dependency.monitor_id=saved_monitor
observers['video-target-params']();timers[#timers]()
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-peak'])-400)<0.0001,
 'returning to the calibrated monitor must restore its measured peak')
assert(math.abs(p['user-data/hdr-controls'].advanced_target.declared_black-0.5)<0.0001 and
       math.abs(p['user-data/hdr-controls'].advanced_target.swapchain_black-0.203)<0.0001,
 'diagnostics must keep physical and encoded black inputs separate')
p['video-target-params']['min-luma']=0.031514
observers['video-target-params']();timers[#timers]()
assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-black'])-0.203)<0.0001,
 'Advanced output min-luma must not feed back and ratchet the captured SDR black floor')
p['video-target-params']['min-luma']=0.203
print('PASS Advanced DV to SDR keeps a reproducible no-ICM perceptual colour contract')

for _,mapping in ipairs({'relative','desaturate','darken','perceptual'}) do
 callbacks['advanced-mapping'](mapping)
 assert(p['options/gamut-mapping-mode']==mapping,
  'Advanced gamut mapping selector must apply '..mapping)
 assert(p['user-data/hdr-controls'].advanced_target.mapping_mode==mapping,
  'Advanced diagnostics must record '..mapping)
end
callbacks['advanced-mapping']('clip')
assert(p['options/gamut-mapping-mode']=='perceptual',
 'Advanced selector must reject modes outside the four supported strategies')
print('PASS Advanced DV to SDR exposes exactly four independent gpu-next mapping strategies')

callbacks['advanced-calibration']('system-icc')
assert(p['options/gpu-next-dovi-sdr-adaptive']==false,
 'adaptive highlights must not run with the display ICC selected')
assert(p['options/icc-profile-auto']==true and p['options/icc-profile']=='' and
       p['options/target-lut']=='',
 'ordinary Advanced colour management must use the installed system ICC exactly once')
assert(p['user-data/hdr-controls'].advanced_target.calibration_mode=='system-icc',
 'ordinary calibration mode must be visible in diagnostics')
callbacks['advanced-calibration']('displaycal-lut')
assert(p['options/gpu-next-dovi-sdr-adaptive']==false,
 'adaptive highlights must not run with the display LUT selected')
assert(p['options/icc-profile-auto']==false and
       p['options/target-lut']=='D:\\calibration\\displaycal.cube',
 'expert Advanced colour management must use the selected DisplayCAL LUT without system ICC')
assert(p['user-data/hdr-controls'].advanced_target.calibration_mode=='displaycal-lut' and
       p['user-data/hdr-controls'].advanced_target.calibration_state=='active',
 'DisplayCAL LUT state must be visible in diagnostics')
callbacks['advanced-calibration']('none')
assert(p['options/gpu-next-dovi-sdr-adaptive']==adaptive_requested,
 'returning to direct SDR restores the explicit adaptive preference')
assert(p['options/icc-profile-auto']==false and p['options/target-lut']=='',
 'direct BT.709/sRGB output must remain available as the accepted no-ICM reference')
print('PASS Advanced colour management separates system ICC, DisplayCAL LUT and direct output')

p['video-params']={gamma='hlg',colormatrix='dolbyvision',w=3840,h=2160}
p['video-target-params'].w=2560;p['video-target-params'].h=1440
observers['video-params']();timers[#timers]()
assert(p['options/gpu-next-dovi-sdr-staged']==true,'50e88c0 reference must not silently switch 4K to the later single-pass colour path')
print('PASS high-resolution P8.4 retains the accepted staged reference path')

for _,source in ipairs({
 {profile=5,tag='dvhe.05',transfer='ipt-pq-c2'},
 {profile=7,tag='dvhe.07',transfer='pq'},
 {profile=8,tag='dvhe.08',transfer='pq'},
 {profile=9,tag='dvav.09',transfer='bt.1886'},
 {profile=10,tag='dav1.10',transfer='pq'},
}) do
 p['video-params']={gamma=source.transfer,colormatrix='dolbyvision'}
 p['user-data/dv-playback']={profile=source.profile,profile_string=source.tag,base_transfer=source.transfer}
 p['user-data/windows-dv-inwindow'].source_base_transfer=source.transfer
 observers['video-params']();timers[#timers]()
 local state=p['user-data/hdr-controls']
 assert(state.advanced_target.profile==source.profile and state.advanced_target.profile_string==source.tag,
  source.tag..' must be identified in the Advanced colour route')
 assert(math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-peak'])-400)<0.0001 and
        math.abs(tonumber(p['options/gpu-next-dovi-sdr-display-black'])-0.203)<0.0001,
  source.tag..' must receive the same active-display envelope')
 assert(p['options/gpu-next-dovi-sdr-staged']==false,
  source.tag..' must use ordinary gpu-next when it is not single-layer HLG P8.4')
 assert(state.advanced_target.dynamic_mapping==true and
        state.advanced_target.dynamic_path=='probe-32x18',
  source.tag..' must retain dynamic display mapping through the low-cost probe path')
end
print('PASS P5, P7, P8.1, P9 and P10 all retain Advanced dynamic display mapping')

-- RPU-off is now a real compatible-base-layer comparison. Once the format
-- filter restores PQ/HLG + BT.2020, the DV-only target must release control so
-- ordinary gpu-next can tone-map that base layer without stale DV settings.
p['user-data/dv-playback']={profile=7,profile_string='dvhe.07',base_transfer='pq',rpu_disabled=true}
p['video-params']={gamma='pq',colormatrix='bt.2020-ncl'}
observers['video-params']();timers[#timers]()
local base_only=p['user-data/hdr-controls'].advanced_target
assert(p['options/gpu-next-dovi-sdr-adaptive']==false,
 'compatible BL-only comparison must not inherit a DV adaptive highlight effect')
assert(base_only.route=='gpu-next-auto' and base_only.reason=='rpu-off-base-layer',
 'RPU-off comparison must release the DV-only mapping to ordinary gpu-next')
assert(p['options/target-trc']=='auto' and p['options/target-prim']=='auto' and
       tonumber(p['options/gpu-next-dovi-sdr-display-peak'])==0,
 'BL-only comparison must not retain a stale Advanced DV target')
print('PASS RPU-off comparison restores the compatible base-layer gpu-next route')

-- The custom 50e88c0 route is deliberately bounded to DV -> SDR-only output.
-- An HDR-active output keeps the source in ordinary gpu-next auto negotiation.
p['user-data/windows-dv-inwindow'].dependency.display_color_mode='hdr'
p['user-data/windows-dv-inwindow'].dependency.display_hdr_supported=true
p['user-data/windows-dv-inwindow'].dependency.display_hdr_active=true
p['user-data/dv-playback']={profile=8,profile_string='dvhe.08',base_transfer='pq'}
p['video-params']={gamma='pq',colormatrix='dolbyvision'}
observers['video-params']();timers[#timers]()
local hdr_target=p['user-data/hdr-controls'].advanced_target
assert(p['options/gpu-next-dovi-sdr-adaptive']==false,
 'HDR output must not inherit a DV SDR adaptive highlight effect')
assert(hdr_target.route=='gpu-next-auto' and hdr_target.display_color_mode=='hdr',
 'HDR output must bypass the custom DV-to-SDR mapping')
assert(p['options/target-trc']=='auto' and p['options/target-prim']=='auto',
 'HDR output colour target must be negotiated by gpu-next')
assert(p['options/gpu-next-dovi-sdr-staged']==false,
 'SDR-only staged mapping must never run on an HDR output')
assert(p['options/icc-profile-auto']==false,
 'the SDR ICC must not be applied over an HDR output')
print('PASS Advanced custom mapping is bounded to DV on an SDR-only output')

-- Do not guess SDR while the per-output Advanced Color probe is pending.
p['user-data/windows-dv-inwindow'].dependency.display_color_mode='unknown'
p['user-data/windows-dv-inwindow'].dependency.display_hdr_supported=nil
p['user-data/windows-dv-inwindow'].dependency.display_hdr_active=nil
observers['video-params']();timers[#timers]()
local unknown_target=p['user-data/hdr-controls'].advanced_target
assert(unknown_target.route=='gpu-next-auto' and p['options/gpu-next-dovi-sdr-staged']==false,
 'unknown output capability must fail closed to ordinary gpu-next')
assert(p['options/icc-profile-auto']==false,'unknown output must not receive an assumed SDR ICC')
print('PASS pending display capability fails closed to gpu-next auto')

-- HDR10/HLG without DV is always ordinary gpu-next, even on an SDR display.
p['user-data/windows-dv-inwindow'].dependency.display_color_mode='sdr'
p['user-data/windows-dv-inwindow'].dependency.display_hdr_supported=false
p['user-data/windows-dv-inwindow'].dependency.display_hdr_active=false
p['user-data/windows-dv-inwindow'].source_dv=false
p['user-data/dv-playback']={profile=0,profile_string='',base_transfer='pq'}
p['video-params']={gamma='pq',colormatrix='bt.2020-ncl'}
observers['video-params']();timers[#timers]()
local hdr10_target=p['user-data/hdr-controls'].advanced_target
assert(hdr10_target.route=='gpu-next-auto' and p['options/target-trc']=='auto',
 'non-DV HDR must use the normal gpu-next tone mapper')
print('PASS non-DV HDR remains owned by ordinary gpu-next')

p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true,native_sdr_output=true,p8_native_sdr=true}
observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['options/gpu-next-dovi-sdr-staged']==false,'official Windows SDR output must not be remapped by the open staged route')
assert(p['options/target-peak']=='auto','Windows Dolby output owns its display luminance')
assert(tonumber(p['options/gpu-next-dovi-sdr-display-peak'])==0,'Windows Dolby output must release the open renderer display target')
print('PASS Windows Dolby route and Advanced color management remain exclusive')
