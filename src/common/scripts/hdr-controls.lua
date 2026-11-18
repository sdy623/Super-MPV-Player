-- HDR/DV diagnostics and session-local gpu-next controls.
-- Native playback is an optional separate MediaEngine window. Never label PQ as DV.
local utils=require 'mp.utils'
local options={start_mode='display-auto',advanced_reference='50e88c0',advanced_peak='auto',advanced_full_frame='auto',advanced_black='auto',advanced_staged='yes',advanced_signal_anchor='auto',advanced_mapping='auto',advanced_calibration='none',advanced_lut='',advanced_profile_file='script-opts/advanced_display_profiles.json',advanced_adaptive_highlights='no'}
pcall(function()require('mp.options').read_options(options,'advanced_color_management')end)
local adaptive_requested=tostring(options.advanced_adaptive_highlights):lower()=='yes'
local adaptive_option='gpu-next-dovi-sdr-adaptive'
local adaptive_supported=mp.get_property_native('options/'..adaptive_option)~=nil
local display_profiles={}
-- Device-specific viewing preset, not a measurement or a new Windows default.
local builtin_display_profiles={
    ['DISPLAY\\BOE0CFB']={peak_nits=616,black_nits=0.203,signal_anchor_nits=270,
        gamut_mapping='darken',source='BOE0CFB Advanced SDR preset'},
}
local function monitor_profile(dependency)
    local id=tostring(dependency.monitor_id or '')
    local profile={}
    for k,v in pairs(builtin_display_profiles[id:upper()] or {}) do profile[k]=v end
    local user=display_profiles[id] or display_profiles[tostring(dependency.display_name or '')]
    if type(user)=='table' then
        for k,v in pairs(user) do profile[k]=v end
    end
    return profile
end
do
    local requested=tostring(options.advanced_profile_file or '')
    local path=requested~='' and ((mp.find_config_file and mp.find_config_file(requested)) or requested) or nil
    if path then
        local file=io.open(path,'rb')
        if file then
            local raw=file:read('*a');file:close()
            local ok,parsed=pcall(utils.parse_json,raw)
            if ok and type(parsed)=='table' and type(parsed.profiles)=='table' then
                display_profiles=parsed.profiles
            end
        end
    end
end
local advanced_mapping_modes={'perceptual','relative','desaturate','darken'}
local advanced_mapping_labels={
    perceptual='感知压缩',relative='相对色度边界',
    desaturate='去饱和保高光',darken='降低亮度保色域',
}
local function normalize_advanced_mapping(value)
    value=tostring(value or ''):lower()
    for _,allowed in ipairs(advanced_mapping_modes) do
        if value==allowed then return value end
    end
    return 'perceptual'
end
local advanced_mapping_override
local function effective_advanced_mapping(dependency)
    if advanced_mapping_override then return advanced_mapping_override end
    if tostring(options.advanced_mapping):lower()~='auto' then
        return normalize_advanced_mapping(options.advanced_mapping)
    end
    return normalize_advanced_mapping(monitor_profile(dependency).gamut_mapping)
end
local advanced_calibration_modes={'none','system-icc','displaycal-lut'}
local advanced_calibration_labels={
    none='设备 SDR 直出（无 ICM）',
    ['system-icc']='普通：系统安装 ICC',
    ['displaycal-lut']='高级：DisplayCAL 3D LUT',
}
local function normalize_advanced_calibration(value)
    value=tostring(value or ''):lower()
    for _,allowed in ipairs(advanced_calibration_modes) do
        if value==allowed then return value end
    end
    return 'none'
end
local advanced_calibration=normalize_advanced_calibration(options.advanced_calibration)
local function normalize_mode(value)
    value=tostring(value or ''):lower()
    if value=='advanced-dv-to-sdr' or value=='advanced-dv-hdr-sdr' then return 'advanced-dv-to-sdr' end
    if value=='sdr-reference' then return 'sdr-reference' end
    return 'display-auto'
end
local mode=normalize_mode(options.start_mode)
local overrides={}
-- Keep original-mapping choices out of Windows-processed SDR frames.
local p8_transfer_saved
local native_icm_saved
local windows_saved
local advanced_keys={'target-colorspace-hint','target-colorspace-hint-mode','target-trc',
    'treat-srgb-as-power22','target-prim','target-peak','hdr-reference-white',
    'target-contrast','hdr-compute-peak','gpu-next-dovi-l1-only','tone-mapping',
    'gpu-next-dovi-sdr-display-peak','gpu-next-dovi-sdr-display-black',
    'gamut-mapping-mode','libplacebo-opts','target-lut','icc-profile','icc-profile-auto'}
local advanced_defaults={}
for _,key in ipairs(advanced_keys) do advanced_defaults[key]=mp.get_property('options/'..key,'') end
local negotiated_target={}
local observed_sdr_black={}
local function use_50e_reference()
    return tostring(options.advanced_reference):lower()=='50e88c0'
end
local function profile_string(profile)
    return ({[5]='dvhe.05',[7]='dvhe.07',[8]='dvhe.08',[9]='dvav.09',[10]='dav1.10',[20]='dvh1.20'})[profile] or ''
end
local function advanced_display_target()
    local explicit_peak=tonumber(options.advanced_peak)
    local explicit_full_frame=tonumber(options.advanced_full_frame)
    local explicit_anchor=tonumber(options.advanced_signal_anchor)
    local explicit_black=tonumber(options.advanced_black)
    local inside=mp.get_property_native('user-data/windows-dv-inwindow',{})
    local dependency=type(inside.dependency)=='table' and inside.dependency or {}
    local device_profile=monitor_profile(dependency)
    local profile_anchor=tonumber(device_profile.signal_anchor_nits)
    if profile_anchor and (profile_anchor<40 or profile_anchor>10000) then profile_anchor=nil end
    local profile_peak=tonumber(device_profile.peak_nits)
    local profile_full_frame=tonumber(device_profile.full_frame_nits)
    local profile_black=tonumber(device_profile.black_nits)
    local target=mp.get_property_native('video-target-params',{})
    local target_peak=tonumber(target['max-luma'])
    local target_black=tonumber(target['min-luma'])
    local declared_peak=tonumber(dependency.display_peak_nits)
    local declared_full_frame=tonumber(dependency.display_full_frame_nits)
    local peak,source
    if explicit_peak and explicit_peak>=40 and explicit_peak<=10000 then
        peak,source=explicit_peak,'user/calibration'
    elseif profile_peak and profile_peak>=40 and profile_peak<=10000 then
        peak,source=profile_peak,tostring(device_profile.source or 'per-display calibration')
    elseif declared_peak and declared_peak>=40 and declared_peak<=10000 then
        peak,source=declared_peak,tostring(dependency.display_target_source or 'ICC/EDID')
    -- 80 nit is normally the Windows SDR composition reference, not panel
    -- capability. Only accept a swapchain target that is clearly above it.
    elseif target_peak and target_peak>=100 and target_peak<=10000 then
        peak,source=target_peak,'swapchain/DXGI'
    end
    local full_frame,full_frame_source
    if explicit_full_frame and explicit_full_frame>=40 and explicit_full_frame<=10000 then
        full_frame,full_frame_source=explicit_full_frame,'user/calibration'
    elseif profile_full_frame and profile_full_frame>=40 and profile_full_frame<=10000 then
        full_frame,full_frame_source=profile_full_frame,
            tostring(device_profile.source or 'per-display calibration')
    elseif declared_full_frame and declared_full_frame>=40 and declared_full_frame<=10000 then
        full_frame,full_frame_source=declared_full_frame,
            tostring(dependency.display_target_source or 'DXGI/EDID')
    elseif peak then
        -- A display without a distinct full-frame report is treated as having
        -- no ABL/local-dimming headroom. Dynamic scene mapping remains valid,
        -- but does not invent a lower sustained-white number.
        full_frame,full_frame_source=peak,'same-as-peak'
    end
    if peak and full_frame then full_frame=math.max(40,math.min(full_frame,peak)) end
    local signal_anchor=explicit_anchor or profile_anchor or full_frame or peak
    if peak and signal_anchor then signal_anchor=math.max(40,math.min(signal_anchor,peak)) end
    local display_key=tostring(dependency.display_name or '')..'|'..tostring(dependency.monitor_id or '')
    if target_black and target_black>0 and target_black<5 and
       not observed_sdr_black[display_key] and
       (dependency.display_color_mode=='sdr' or dependency.display_color_mode=='wcg') then
        -- Capture the pre-Advanced swapchain floor once. The renderer later
        -- reports its own scaled min-luma; feeding that result back here would
        -- ratchet the black target downward on every property update.
        observed_sdr_black[display_key]=target_black
    end
    local swapchain_black=observed_sdr_black[display_key]
    local declared_contrast=tonumber(dependency.display_contrast)
    local declared_black=tonumber(dependency.display_black_nits)
    if (not declared_black or declared_black<=0 or not peak or declared_black>=peak) and
       declared_contrast and declared_contrast>=100 and declared_contrast<=1000000 and peak then
        declared_black=peak/declared_contrast
    end
    local contrast,black
    if explicit_black and explicit_black>0 and peak and explicit_black<peak then
        black=explicit_black;contrast=peak/explicit_black;source=source..'+user-black'
    elseif profile_black and profile_black>0 and peak and profile_black<peak then
        black=profile_black;contrast=peak/profile_black
        source=source..'+per-display-black'
    elseif peak and declared_black and swapchain_black then
        -- A measured/OEM black point describes the physical panel, while the
        -- SDR swapchain minimum describes the code-value floor already chosen
        -- by Windows/libplacebo. Use the darker credible floor so Advanced does
        -- not raise encoded blacks above what the active SDR path can represent.
        black=math.min(declared_black,swapchain_black)
        contrast=peak/black;source=source..'+least-lift-black'
    elseif peak and swapchain_black then
        black=swapchain_black;contrast=peak/black;source=source..'+sdr-target-black'
    elseif peak and declared_black then
        black=declared_black;contrast=peak/black;source=source..'+declared-black'
    end
    negotiated_target={peak=peak,black=black,contrast=contrast,source=source or 'libplacebo fallback',
        full_frame=full_frame,full_frame_source=full_frame_source,signal_anchor=signal_anchor,
        declared_black=declared_black,swapchain_black=swapchain_black,
        device_profile=device_profile,
        display_name=dependency.display_name,monitor_id=dependency.monitor_id,
        dxgi_output=dependency.dxgi_output,icc_profile_data=dependency.icc_profile_data,
        displayid_target=dependency.displayid_target}
    return peak or 0,black or 0,contrast or 0,signal_anchor or 0
end
local function apply_advanced_calibration(set,dependency)
    local requested=advanced_calibration
    local device_profile=type(negotiated_target.device_profile)=='table' and
        negotiated_target.device_profile or {}
    local lut=tostring(options.advanced_lut or '')
    if lut=='' then lut=tostring(device_profile.lut or '') end
    local lut_ok=false
    if lut~='' then
        lut=(mp.find_config_file and mp.find_config_file(lut)) or lut
        local ok,info=pcall(utils.file_info,lut)
        lut_ok=ok and type(info)=='table' and info.is_file==true
    end
    local active=requested
    local state='active'
    if requested=='displaycal-lut' then
        set('icc-profile','');set('icc-profile-auto','no')
        if lut_ok then set('target-lut',lut)
        else set('target-lut','');active='none';state='lut-missing' end
    elseif requested=='system-icc' then
        set('target-lut','');set('icc-profile','');set('icc-profile-auto','yes')
        if not dependency.output_icc or dependency.output_icc=='' then state='profile-pending' end
    else
        set('target-lut','');set('icc-profile','');set('icc-profile-auto','no')
    end
    negotiated_target.calibration_mode=active
    negotiated_target.calibration_requested=requested
    negotiated_target.calibration_state=state
    negotiated_target.calibration_profile=dependency.output_icc
    negotiated_target.calibration_lut=lut_ok and lut or nil
end
local function advanced_staged_enabled()
    local requested=tostring(options.advanced_staged):lower()
    if requested=='yes' or requested=='true' or requested=='1' then return true end
    if requested=='no' or requested=='false' or requested=='0' then return false end
    local target=mp.get_property_native('video-target-params',{})
    local w=tonumber(target.w) or 0
    local h=tonumber(target.h) or 0
    if w<=0 or h<=0 then
        local render=mp.get_property_native('video-out-params',{})
        w=tonumber(render.w) or 0;h=tonumber(render.h) or 0
    end
    -- The staged route uses a full-size FP32 intermediate plus a second render.
    -- Keep it automatic only up through 1080p; high-resolution targets use
    -- libplacebo's single-pass RPU/L1-aware map with the same ICC target.
    return w<=0 or h<=0 or w*h<=1920*1080
end
local windows_neutral={['tone-mapping']='auto',['gamut-mapping-mode']='auto',
 ['hdr-compute-peak']='no',['hdr-contrast-recovery']='0',['tone-mapping-visualize']='no'}
local function windows_owns(inside,key)
 return inside.windows_processed and (key=='target-peak' or ((inside.native_sdr_output or inside.p5_native_sdr) and key=='icc-profile-auto') or
  ((not inside.float_output or inside.relative_srgb_output) and
   (key=='tone-mapping' or key=='gamut-mapping-mode' or key:match('^hdr%-') or key=='tone-mapping-visualize')))
end
local function advanced_contract_owns(key)
    return mode=='advanced-dv-to-sdr' and
        (key=='icc-profile-auto' or key=='target-lut' or key=='gamut-mapping-mode')
end
local timer
local media_active=not mp.get_property_bool('idle-active',true)
local button_json=''
local function indicator(hdr,dv,force)
    local dcp=mp.get_property_native('user-data/dcp',{}).active==true
    local button={icon=dcp and 'palette' or (hdr and 'hdr_on' or 'hdr_off'),active=hdr or dcp,
        badge=dcp and 'DCP' or (dv and 'DV' or nil),
        tooltip=dcp and 'DCP 色彩管理 · XYZ / 系统 ICM' or dv and 'HDR/DV 设置 · 已检测到 Dolby Vision' or
            (hdr and 'HDR/DV 设置 · 已检测到 HDR' or 'HDR/DV 设置 · 当前未检测到 HDR'),
        command={'script-message-to','hdr_controls','menu'}}
    local json=utils.format_json(button)
    if force or json~=button_json then
        button_json=json
        mp.commandv('script-message-to','uosc','set-button','hdr_dv',json)
    end
end
local fields={
    ['tone-mapping']={'auto','spline','bt.2390','bt.2446a','st2094-10','st2094-40','mobius','reinhard','hable'},
    ['gamut-mapping-mode']={'auto','perceptual','relative','saturation','desaturate','clip'},
    ['hdr-compute-peak']={'auto','yes','no'},
    ['target-peak']={'auto','100','160','203','250','300','400','600','1000'},
    ['hdr-contrast-recovery']={'0','0.3','0.5','1','1.5'},
    ['hdr-contrast-smoothness']={'2','3.5','5','8'},
    ['hdr-peak-percentile']={'0','99','99.9','99.995','100'},
    ['hdr-peak-decay-rate']={'5','10','20','40','80'},
    ['tone-mapping-visualize']={'no','yes'},
    ['hwdec']={'auto-safe','d3d11va-copy','no'},
    ['icc-profile-auto']={'yes','no'},
}
local labels={['tone-mapping']='色调映射曲线',['gamut-mapping-mode']='色域映射',
    ['hdr-compute-peak']='逐帧峰值检测',['target-peak']='目标参考亮度（nits）',
    ['hdr-contrast-recovery']='局部对比度恢复',['hdr-contrast-smoothness']='对比度恢复平滑度',
    ['hdr-peak-percentile']='峰值百分位（较低会裁高光）',['hdr-peak-decay-rate']='峰值平滑时间',
    ['tone-mapping-visualize']='显示映射曲线',['hwdec']='解码方式（CPU 仅用于排障）',
    ['icc-profile-auto']='Windows 显示器 ICC/ICM'}
local order={'tone-mapping','gamut-mapping-mode','target-peak','hdr-compute-peak',
    'hdr-peak-percentile','hdr-peak-decay-rate','hdr-contrast-recovery','hdr-contrast-smoothness',
    'tone-mapping-visualize','hwdec','icc-profile-auto'}
local defaults={}
for key in pairs(fields) do defaults[key]=mp.get_property('options/'..key) end
local function set(key,value)
    value=tostring(value)
    local property='options/'..key
    local current=mp.get_property_native(property)
    local desired=tonumber(value)
    if value=='yes' or value=='true' then desired=true
    elseif value=='no' or value=='false' then desired=false
    elseif desired==nil then desired=value end
    if current==desired or tostring(current)==value or
        (type(current)=='number' and type(desired)=='number' and
         math.abs(current-desired)<=math.max(0.000001,math.abs(desired)*0.000001)) then
        return true
    end
    local ok,err
    if type(desired)=='number' or type(desired)=='boolean' then
        ok,err=mp.set_property_native(property,desired)
    else
        ok,err=mp.set_property(property,value)
    end
    if not ok then mp.msg.error(key..': '..tostring(err));mp.osd_message('设置未生效：'..key,4) end
    return ok
end
local function snapshot()
    local src=mp.get_property_native('video-params',{})
    -- Split-layer P7 is composed after the primary decoder. video-params can
    -- still describe the HDR10 base layer while gpu-next receives DV frames.
    local render=mp.get_property_native('video-out-params',{})
    local dst=mp.get_property_native('video-target-params',{})
    local inwindow=mp.get_property_native('user-data/windows-dv-inwindow',{})
    local dv=media_active and (inwindow.source_dv==true or render.colormatrix=='dolbyvision' or src.colormatrix=='dolbyvision') or false
    local hdr=media_active and (dv or src.gamma=='pq' or src.gamma=='hlg' or render.gamma=='pq' or render.gamma=='hlg') or false
    local source=dv and 'Dolby Vision（帧级 RPU 或渲染元数据已识别）' or
        (src.gamma=='pq' and 'PQ / HDR10 基础信号（未检测到帧级 DV）' or
        (src.gamma=='hlg' and 'HLG' or (src.gamma and 'SDR / '..src.gamma or '尚未解码视频')))
    if dv and inwindow.source_base_transfer=='hlg' then
        source='Dolby Vision / HLG 基础层'
    end
    if media_active and mp.get_property_native('user-data/dcp',{}).active==true then source='DCP / XYZ / ST 428' end
    local options={}
    for key in pairs(fields) do options[key]=mp.get_property_native('options/'..key) end
    local result={mode=mode,source=source,source_dv=dv,['source-hdr']=hdr,source_params=src,render_input_params=render,target_params=dst,
        native_dv_output=false,native_dv_reason=inwindow.windows_processed and 'Windows 杜比处理到 SDR；不等于物理 DV HDR 输出' or '未观测到本窗口 Windows 杜比处理；按当前原映射播放',
        inwindow=inwindow,
        native_backend=mp.get_property_native('user-data/windows-native-dv',{}),
        advanced_target=negotiated_target,
        vo=mp.get_property('current-vo','未初始化'),options=options,session_only=true}
    mp.set_property_native('user-data/hdr-controls',result)
    indicator(hdr,dv)
    return result
end
local function apply()
    timer=nil
    -- Resolve route defaults and session overrides before touching the renderer.
    -- Writing ICC=yes then ICC=no on each frame invalidates the LUT twice and
    -- causes flicker even though the final option reports the requested value.
    local pending, ordered = {}, {}
    local commit_property = set
    local function set(key, value)
        if pending[key] == nil then ordered[#ordered + 1] = key end
        pending[key] = value
    end
    local function commit()
        for _, key in ipairs(ordered) do commit_property(key, pending[key]) end
    end
    local function intended(key, default)
        if pending[key] ~= nil then return tostring(pending[key]) end
        return mp.get_property('options/'..key, default)
    end
    local inside=mp.get_property_native('user-data/windows-dv-inwindow',{})
    local dcp=mp.get_property_native('user-data/dcp',{}).active==true
    local relative=inside.windows_processed and inside.relative_srgb_output and not dcp
    local p5=inside.windows_processed and inside.p5_native_sdr and not dcp
    local native_sdr=inside.windows_processed and (inside.native_sdr_output or inside.p5_native_sdr) and not dcp
    local playback=mp.get_property_native('user-data/dv-playback',{})
    local profile=tonumber(playback.profile) or 0
    local source_profile_string=tostring(playback.profile_string or profile_string(profile))
    local rpu_base_comparison=playback.rpu_disabled==true
    local dependency=type(inside.dependency)=='table' and inside.dependency or {}
    local display_color_mode=tostring(dependency.display_color_mode or 'unknown'):lower()
    local display_sdr_only=(display_color_mode=='sdr' or display_color_mode=='wcg') and
        dependency.display_hdr_supported==false
    local advanced_source_dv=mode=='advanced-dv-to-sdr' and not rpu_base_comparison and
        not inside.windows_processed and not dcp and
        (profile>0 or inside.source_dv==true)
    -- The custom 50e88c0 mapping has one product boundary: a DV source on an
    -- output that Windows positively identifies as SDR-only. HDR-capable,
    -- HDR-active and not-yet-probed outputs stay on ordinary gpu-next.
    local advanced_dv=advanced_source_dv and display_sdr_only
    local advanced_mapping=effective_advanced_mapping(dependency)
    local staged_dv=advanced_dv and ((profile==8 and playback.base_transfer=='hlg') or
        (inside.source_dv==true and inside.source_base_transfer=='hlg'))
    -- The optional appearance pass belongs only to the open DV -> SDR route.
    -- Do not write unknown options in configuration-only builds of mpv.
    local adaptive_enabled=advanced_dv and adaptive_requested and advanced_calibration=='none' and adaptive_supported
    if adaptive_supported then set(adaptive_option,adaptive_enabled and 'yes' or 'no') end
    if not advanced_dv then
        -- Do not leak the SDR-only candidate into ordinary SDR/HDR, an HDR
        -- output, Windows-processed RGB, DCP or the pre-probe state.
        negotiated_target={}
        set('gpu-next-dovi-sdr-staged','no')
        for _,key in ipairs(advanced_keys) do set(key,advanced_defaults[key]) end
    end
    set('gpu-next-dovi-sdr-signal-anchor','0')
    -- This route carries negotiated, already encoded Dolby SDR values. Use the
    -- same explicit sRGB function on input/output; preserve other profile policy.
    if inside.p8_native_sdr and native_sdr then
        if not p8_transfer_saved then
            p8_transfer_saved=intended('treat-srgb-as-power22','auto')
        end
        set('treat-srgb-as-power22','no')
    elseif p8_transfer_saved then
        set('treat-srgb-as-power22',p8_transfer_saved)
        p8_transfer_saved=nil
    end
    if native_sdr then
        if not native_icm_saved then
            native_icm_saved={auto=intended('icc-profile-auto','yes'),profile=intended('icc-profile','')}
        end
    elseif native_icm_saved then
        set('icc-profile',native_icm_saved.profile)
        set('icc-profile-auto',native_icm_saved.auto)
        native_icm_saved=nil
    end
    if mp.get_property_native('user-data/dcp',{}).active==true then
        commit()
        mp.set_property_bool('file-local-options/icc-profile-auto',true)
        snapshot();return
    end
    local inside=mp.get_property_native('user-data/windows-dv-inwindow',{})
    if relative then
        -- Select the established relative-sRGB output contract directly. Do not
        -- temporarily switch to the source HDR curve on each RPU update.
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc',inside.p8_native_sdr and 'srgb' or 'auto');set('target-prim','bt.709');set('target-peak','auto')
        if native_sdr then set('icc-profile','');set('icc-profile-auto','no')
        else set('icc-profile-auto',overrides['icc-profile-auto'] or 'yes') end
    elseif inside.windows_processed and not inside.float_output then
        -- The frame processor returns device-referred SDR RGB with the system ICC.
        -- Keep its matching target profile; do not map that SDR output to HDR again.
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','bt.1886');set('target-prim','bt.709');set('target-peak','auto')
        set('icc-profile-auto','yes')
    elseif advanced_dv then
        local staged_enabled=staged_dv and advanced_staged_enabled()
        -- Keep Windows SDR reference-white semantics separate from the physical
        -- target. Prefer the swapchain/DXGI/EDID-derived target peak; a user may
        -- override it after measurement in advanced_color_management.conf.
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','srgb');set('treat-srgb-as-power22','no');set('target-prim','bt.709')
        set('hdr-reference-white','auto')
        set('hdr-compute-peak','auto');set('gpu-next-dovi-l1-only','no')
        -- This mode owns its complete SDR colour contract. Dolby/RPU tone
        -- mapping happens before an explicit BT.2020/IPT -> BT.709 gamut map;
        -- the final device transform is selected exactly once below.
        set('tone-mapping','auto');set('gamut-mapping-mode',advanced_mapping)
        set('libplacebo-opts','')
        if use_50e_reference() then
            -- Preserve the user-accepted 50e88c0 processing order: staged
            -- FP32 P8.4 mapping into a complete nominal BT.709/sRGB output.
            -- Peak/black remain per-display inputs from user calibration,
            -- ICC/ICM, DisplayID/EDID and DXGI; 400 is not baked in.
            local peak,black,_,signal_anchor=advanced_display_target()
            set('target-peak','auto')
            set('target-contrast','auto')
            set('gpu-next-dovi-sdr-display-peak',peak)
            set('gpu-next-dovi-sdr-display-black',black)
            set('gpu-next-dovi-sdr-signal-anchor',signal_anchor)
            set('gpu-next-dovi-sdr-staged',staged_enabled and 'yes' or 'no')
            negotiated_target.reference='50e88c0-color-path'
        else
            -- Optional device-capability experiment retained for explicit
            -- configuration, but it is no longer the shipped Advanced preset.
            local display_peak,display_black,_,signal_anchor=advanced_display_target()
            set('gpu-next-dovi-sdr-display-peak',display_peak)
            set('gpu-next-dovi-sdr-display-black',display_black)
            set('gpu-next-dovi-sdr-signal-anchor',signal_anchor)
            set('gpu-next-dovi-sdr-staged',staged_enabled and 'yes' or 'no')
        end
        apply_advanced_calibration(set,dependency)
        negotiated_target.profile=profile
        negotiated_target.profile_string=source_profile_string
        negotiated_target.renderer='gpu-next/libplacebo'
        negotiated_target.route='dv-to-sdr-only'
        negotiated_target.mapping_mode=advanced_mapping
        negotiated_target.dynamic_mapping=true
        negotiated_target.dynamic_path=staged_enabled and 'staged-fp32' or 'probe-32x18'
        negotiated_target.adaptive_highlights_requested=adaptive_requested
        negotiated_target.adaptive_highlights_enabled=adaptive_enabled
        negotiated_target.display_color_mode=display_color_mode
        negotiated_target.display_hdr_supported=false
        negotiated_target.display_hdr_active=false
    elseif mode=='advanced-dv-to-sdr' then
        -- Everything outside the DV -> SDR-only boundary uses gpu-next's
        -- normal display negotiation. In particular, never force an HDR
        -- output back to BT.709/sRGB or apply the SDR ICC over it.
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','auto');set('target-prim','auto')
        set('target-peak','auto');set('target-contrast','auto')
        set('hdr-reference-white','auto');set('hdr-compute-peak','auto')
        set('gpu-next-dovi-l1-only','no')
        set('gpu-next-dovi-sdr-display-peak',0);set('gpu-next-dovi-sdr-display-black',0)
        set('gpu-next-dovi-sdr-signal-anchor',0)
        set('gpu-next-dovi-sdr-staged','no')
        set('target-lut','');set('icc-profile','');set('icc-profile-auto','no')
        negotiated_target={route='gpu-next-auto',renderer='gpu-next/libplacebo',
            profile=profile,profile_string=source_profile_string,
            reason=rpu_base_comparison and 'rpu-off-base-layer' or nil,
            display_color_mode=display_color_mode,
            display_hdr_supported=dependency.display_hdr_supported,
            display_hdr_active=dependency.display_hdr_active}
    elseif mode=='sdr-reference' then
        local src=mp.get_property_native('video-params',{})
        local hdr=src.gamma=='pq' or src.gamma=='hlg' or src.colormatrix=='dolbyvision'
        set('target-colorspace-hint',hdr and 'yes' or 'auto')
        set('target-colorspace-hint-mode','target')
        set('target-trc',hdr and 'bt.1886' or 'auto')
        set('target-prim',hdr and 'bt.709' or 'auto')
        set('target-peak','auto')
        set('icc-profile-auto','yes')
    elseif mode=='display-auto' then
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','auto');set('target-prim','auto');set('target-peak','auto')
        -- Do not apply the SDR factory ICM over an HDR output target.
        set('icc-profile-auto','no')
    end
    if windows_owns(inside,'tone-mapping') then
        if not windows_saved then
            windows_saved={}
            for key in pairs(windows_neutral) do windows_saved[key]=mp.get_property('options/'..key) end
        end
        for key,value in pairs(windows_neutral) do set(key,value) end
    elseif windows_saved then
        for key,value in pairs(windows_saved) do if value then set(key,value) end end
        windows_saved=nil
    end
    for key,value in pairs(overrides) do
        if not windows_owns(inside,key) and not advanced_contract_owns(key) then set(key,value) end
    end
    if mode=='advanced-dv-to-sdr' then
        if not advanced_dv then
            set('target-lut','');set('icc-profile','');set('icc-profile-auto','no')
        end
        set('gamut-mapping-mode',advanced_dv and advanced_mapping or 'auto')
    end
    if inside.windows_processed then set('target-peak','auto') end
    commit()
    snapshot()
end
local function schedule()
    if timer then timer:kill() end
    timer=mp.add_timeout(0.08,apply)
end
local function menu()
    if mp.get_property_native('user-data/dcp',{}).active==true then mp.commandv('script-message','dcp-color-menu');return end
    local s=snapshot()
    local native=s.native_backend or {}
    local inside=s.inwindow or {}
    local playback=mp.get_property_native('user-data/dv-playback',{})
    local items={{title='输入：'..s.source,selectable=false,muted=true,value={'ignore'}},
        {title='输出：'..s.vo..' / '..(s.target_params.gamma or '未初始化'),selectable=false,muted=true,value={'ignore'}},
        {title='Windows 杜比处理（MPV 本窗口）',hint=inside.available and (inside.badge or '等待状态') or '依赖未就绪：检查官方扩展与本地适配器',items={
            {title='重新检测官方依赖',value={'script-message','windows-dv-dependencies-refresh'}},
            {title='启用 Windows 杜比处理（实验）',selectable=inside.available==true,muted=not inside.available,active=inside.enabled,value={'script-message','windows-dv-inwindow-enable'}},
            {title='切回原 gpu-next / SDR 映射',active=not inside.enabled,value={'script-message','windows-dv-inwindow-disable'}},
            {title=inside.source_base_transfer=='hlg' and (inside.windows_processed and 'HLG 已按源格式交给 Windows 杜比处理，输出 SDR' or 'HLG 保留 DV 元数据，由 gpu-next 映射；Windows 处理未生效') or
                ('状态：'..(inside.fallback_reason~='' and inside.fallback_reason or (inside.badge or '等待视频'))),
                selectable=false,muted=true,value={'ignore'}},
            {title='RPU '..tostring(inside.rpu_seen or 0)..' / Windows处理帧 '..tostring(inside.processed_frames or 0),selectable=false,muted=true,value={'ignore'}},
            {title='内屏输出 SDR；不是物理 DV HDR 输出',selectable=false,muted=true,value={'ignore'}}}},
        {title='旧实验：Windows 独立窗口（非本窗口）',hint=native.available and '可选；不替换 MPV 默认后端' or '后端检测中 / 未安装',
            selectable=native.available==true,muted=not native.available,items={
                {title='用 Windows 原生后端播放当前文件',value={'script-message-to','windows_native_dv','start'}},
                {title='原生窗口：暂停 / 继续',value={'script-message-to','windows_native_dv','toggle'}},
                {title='关闭原生窗口，返回 MPV',value={'script-message-to','windows_native_dv','stop'}},
                {title='状态：'..(native.state or '未检测'),selectable=false,muted=true,value={'ignore'}},
                {title='系统声明：'..table.concat(native.profiles or {},' / '),selectable=false,muted=true,value={'ignore'}},
                {title='原生输入：'..((native.native or {}).source_dv_profile or '未开始'),selectable=false,muted=true,value={'ignore'}},
                {title='已呈现帧：'..tostring((native.native or {}).frames_rendered or 0)..' / 丢帧：'..tostring((native.native or {}).frames_dropped or 0),selectable=false,muted=true,value={'ignore'}},
                {title='解码后 RPU 转发：'..(((native.native or {}).rpu_forwarding_observed) and (tostring((native.native or {}).forwarded_rpu_units)..' 个单元') or '未观测'),selectable=false,muted=true,value={'ignore'}},
                {title='RPU 转发不等于效果消费；DV HDR 显示输出仍须验收',selectable=false,muted=true,value={'ignore'}},
                {title='F1 / P7 原盘继续 MPV；不自动转换或丢增强层',selectable=false,muted=true,value={'ignore'}},
                {title='重新检测系统扩展',value={'script-message-to','windows_native_dv','probe'}}}},
        {title='Advanced DV to SDR（独立动态映射）',active=mode=='advanced-dv-to-sdr' and inside.enabled~=true,value={'script-message-to','hdr_controls','mode','advanced-dv-to-sdr'}},
        {title='SDR 映射 + 系统 ICC（参考亮度 auto）',active=mode=='sdr-reference',value={'script-message-to','hdr_controls','mode','sdr-reference'}},
        {title='跟随显示器：自动 SDR / HDR10（不是原生 DV）',active=mode=='display-auto',value={'script-message-to','hdr_controls','mode','display-auto'}}}
    local advanced_mapping=effective_advanced_mapping(type(inside.dependency)=='table' and inside.dependency or {})
    local mapping_items={}
    for _,value in ipairs(advanced_mapping_modes) do
        mapping_items[#mapping_items+1]={title=advanced_mapping_labels[value],active=advanced_mapping==value,
            value={'script-message-to','hdr_controls','advanced-mapping',value}}
    end
    items[#items+1]={title='Advanced 色域映射：'..advanced_mapping_labels[advanced_mapping],
        hint='四种 gpu-next 策略；仅 Advanced DV to SDR 生效',
        selectable=mode=='advanced-dv-to-sdr',muted=mode~='advanced-dv-to-sdr',items=mapping_items}
    local calibration_items={}
    local current_target=(mp.get_property_native('user-data/hdr-controls',{}).advanced_target or {})
    local profile_lut=type(current_target.device_profile)=='table' and
        tostring(current_target.device_profile.lut or '') or ''
    local lut_configured=tostring(options.advanced_lut or '')~='' or profile_lut~=''
    for _,value in ipairs(advanced_calibration_modes) do
        local selectable=value~='displaycal-lut' or lut_configured
        calibration_items[#calibration_items+1]={title=advanced_calibration_labels[value],
            active=advanced_calibration==value,selectable=selectable,muted=not selectable,
            hint=value=='displaycal-lut' and (selectable and tostring(options.advanced_lut) or '先在 advanced_color_management.conf 指定 advanced_lut') or nil,
            value={'script-message-to','hdr_controls','advanced-calibration',value}}
    end
    items[#items+1]={title='Advanced 色彩管理：'..advanced_calibration_labels[advanced_calibration],
        hint='系统 ICC 与 DisplayCAL LUT 二选一；不会双重叠加',
        selectable=mode=='advanced-dv-to-sdr',muted=mode~='advanced-dv-to-sdr',items=calibration_items}
    if mode=='advanced-dv-to-sdr' then
        local dynamic=(mp.get_property_native('user-data/hdr-controls',{}).advanced_target or {}).dynamic_path
        items[#items+1]={title='Advanced 动态显示映射：'..(dynamic=='staged-fp32' and 'FP32 两阶段' or dynamic=='probe-32x18' and '32×18 低开销探针' or '等待 DV 帧'),
            hint='P5 / P7 / P8.1 / P8.4 / P10 均使用场景自适应峰黑包络',
            selectable=false,muted=true,value={'ignore'}}
    end
    table.insert(items,2,{title='源传递函数：'..(playback.base_transfer=='hlg' and 'HLG / ARIB STD-B67' or (playback.base_transfer or 'unknown'))..' → 渲染输入：'..(playback.render_transfer or 'unknown'),
        selectable=false,muted=true,value={'ignore'}})
    if playback.dual_layer then
        table.insert(items,4,{title=playback.el_disabled and '增强层 EL：已关闭 · 点击恢复' or '增强层 EL：已启用 · 点击关闭',
            selectable=playback.can_toggle_el==true,muted=not playback.can_toggle_el,
            hint=playback.rpu_disabled and 'RPU 关闭时已经是 BL-only；恢复 RPU 后才能单独比较 EL' or
                not playback.can_toggle_el and '处理器控制接口未就绪' or
                playback.el_disabled and '当前后端不变；缺少 EL 画面贡献' or '仅当前文件；开关不改变后端',
            active=not playback.el_disabled,value={'script-message','dv-enhancement-toggle'}})
    end
    table.insert(items,4,{title=playback.rpu_disabled and 'RPU/EL：已关闭 · BL-only' or 'RPU/EL：完整 DV 路径',
        hint=playback.profile==5 and 'P5 必须保留 RPU 色彩解释' or
            (playback.rpu_disabled and '点击恢复 RPU reshape / trim 与 EL 重建' or '点击切换为兼容基础层，画面对比应立即可见'),
        selectable=playback.can_toggle_rpu,muted=not playback.can_toggle_rpu,
        value={'script-message','dv-rpu-toggle'}})
    table.insert(items,1,{title='视频信息 / Video information',value={'script-message','video-details-toggle'}})
    for _,key in ipairs(order) do
        local sub={}
        for _,value in ipairs(fields[key]) do
            local actual=key=='target-peak' and tostring(options.advanced_peak) or mp.get_property('options/'..key,'')
            local selected=actual==value or (tonumber(actual) and tonumber(actual)==tonumber(value))
            sub[#sub+1]={title=value,active=selected,value={'script-message-to','hdr_controls','set',key,value}}
        end
        local fixed_by_windows=windows_owns(inside,key)
        local fixed_by_advanced=advanced_contract_owns(key)
        local fixed=fixed_by_windows or fixed_by_advanced
        items[#items+1]={id=key,title=labels[key],hint=fixed_by_advanced and (key=='gamut-mapping-mode' and '由上方四种 Advanced 策略控制' or '由上方 Advanced 色彩管理控制，ICC 与 LUT 不叠加') or ((inside.native_sdr_output or inside.p5_native_sdr) and key=='icc-profile-auto') and 'Dolby SDR 直出：MPV 不再叠加 ICM' or fixed_by_windows and '先切回原映射再调整' or mp.get_property('options/'..key,''),selectable=not fixed,muted=fixed,items=sub}
    end
    items[#items+1]={title='重置：Advanced DV to SDR 默认值',value={'script-message-to','hdr_controls','reset'}}
    items[#items+1]={title='参数仅本次运行有效；不改原盘、不转码',selectable=false,muted=true,value={'ignore'}}
    local data={type='hdr-controls',title='HDR / Dolby Vision / 色调映射',items=items}
    mp.set_property_native('user-data/hdr-menu',data)
    mp.commandv('script-message-to','uosc','open-menu',utils.format_json(data))
end
mp.register_script_message('menu',menu)
mp.register_script_message('set',function(key,value)
    if windows_owns(mp.get_property_native('user-data/windows-dv-inwindow',{}),key) or advanced_contract_owns(key) then return end
    for _,allowed in ipairs(fields[key] or {}) do
        if value==allowed then
            if key=='target-peak' and mode=='advanced-dv-to-sdr' then options.advanced_peak=value
            else overrides[key]=value end
            apply();menu();return
        end
    end
    mp.msg.warn('Rejected unsupported HDR option/value')
end)
mp.register_script_message('advanced-mapping',function(value)
    local requested=normalize_advanced_mapping(value)
    if requested~=tostring(value or ''):lower() then
        mp.msg.warn('Rejected unsupported Advanced gamut mapping mode')
        return
    end
    advanced_mapping_override=requested
    apply();menu()
end)
mp.register_script_message('advanced-calibration',function(value)
    local requested=normalize_advanced_calibration(value)
    if requested~=tostring(value or ''):lower() then
        mp.msg.warn('Rejected unsupported Advanced calibration mode')
        return
    end
    advanced_calibration=requested
    apply();menu()
end)
mp.register_script_message('mode',function(value)
    local requested=normalize_mode(value)
    -- A manual colour-management selection owns the frame. Release the
    -- automatic Windows Dolby filter instead of stacking both renderers.
    mp.commandv('script-message','windows-dv-inwindow-disable')
    mode=requested;overrides['target-peak']=nil;overrides['icc-profile-auto']=nil
    snapshot();schedule();mp.add_timeout(0.12,menu)
end)
mp.register_script_message('reset',function()
    mp.commandv('script-message','windows-dv-inwindow-disable')
    overrides={};mode=normalize_mode(options.start_mode)
    for key,value in pairs(defaults) do if value then set(key,value) end end
    snapshot();schedule();mp.add_timeout(0.12,menu)
end)
local processed_before=nil
mp.observe_property('user-data/windows-dv-inwindow','native',function(_,v)
    local active=type(v)=='table' and (tostring(v.windows_processed==true)..':'..tostring(v.float_output==true)..':'..tostring(v.relative_srgb_output==true)..':'..tostring(v.p5_native_sdr==true)..':'..tostring(v.p8_native_sdr==true)..':'..tostring(v.native_sdr_output==true)) or 'false:false'
    if active~=processed_before then processed_before=active;schedule() end
    snapshot()
end)
mp.observe_property('user-data/dcp','native',schedule)
mp.observe_property('user-data/dv-playback','native',schedule)
mp.observe_property('video-params','native',schedule)
mp.observe_property('video-out-params','native',schedule)
mp.observe_property('video-target-params','native',schedule)
mp.register_event('file-loaded',function()
    media_active=true
    -- native-controls.lua owns the Windows processor policy. Do not undo its
    -- dependency-checked auto-enable here: when Windows owns the frame, apply()
    -- releases the open advanced fallback and its conflicting local options.
    snapshot();schedule()
end)
mp.register_event('start-file',function()media_active=false;snapshot()end)
mp.register_event('end-file',function()media_active=false;snapshot()end)
mp.register_script_message('uosc-version',function()
    mp.add_timeout(0.1,function()
        local s=snapshot();indicator(s['source-hdr'],s.source_dv,true)
    end)
end)
mp.add_timeout(0.2,function()
    local s=snapshot();indicator(s['source-hdr'],s.source_dv,true)
end)
snapshot()
