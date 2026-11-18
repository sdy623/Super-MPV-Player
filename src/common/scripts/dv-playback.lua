-- Source tags, effective renderer input and optional EL contribution are distinct.
local mp=require 'mp'
local utils=require 'mp.utils'
local active=not mp.get_property_bool('idle-active',true)
local dual_seen=false
local announced=''
local overlay=mp.create_osd_overlay('ass-events')
local hide_timer
local label='dv_el_off'
local rpu_label='dv_rpu_off'
local function native_meta()return mp.get_property_native('vf-metadata/windows_dv',{})end
local function profile_string(profile)
    return ({[5]='dvhe.05',[7]='dvhe.07',[8]='dvhe.08',[9]='dvav.09',[10]='dav1.10',[20]='dvh1.20'})[profile] or ''
end
local function filter_has(label)
 for _,f in ipairs(mp.get_property_native('vf',{}))do if f.label==label then return true end end
 return false
end
local function rpu_disabled()
 if mp.get_property_native('user-data/windows-dv-inwindow',{}).enabled then
  return native_meta()['rpu-requested']=='off'
 end
 return filter_has(rpu_label)
end
local button_key
local function el_disabled()
    if filter_has(rpu_label) then return true end
    if native_meta()['el-requested']=='off' then return true end
    for _,f in ipairs(mp.get_property_native('vf',{})) do
        if f.label==label then return true end
    end
    return false
end
local function notice(text)
    if hide_timer then hide_timer:kill() end
    overlay.res_x=1280;overlay.res_y=720
    overlay.data='{\\an9\\pos(1256,76)\\fnSegoe UI\\fs22\\bord1.5\\shad0\\1c&HFFFFFF&}'..text
    overlay:update()
    hide_timer=mp.add_timeout(3,function()overlay:remove();hide_timer=nil end)
end
local function update()
    local src=mp.get_property_native('video-params',{})
    local out=mp.get_property_native('video-out-params',{})
    local win=mp.get_property_native('user-data/windows-dv-inwindow',{})
    local p7,dependent,declared_el=false,false,false
    local profile=tonumber(native_meta()["source-profile"]) or 0
    for _,t in ipairs(mp.get_property_native('track-list',{})) do
        if t.type=='video' then
            if t.selected and t['dolby-vision-profile'] then profile=tonumber(t['dolby-vision-profile']) or profile end
            p7=p7 or (t.selected and tonumber(t['dolby-vision-profile'])==7)
            dependent=dependent or t.dependent==true
            declared_el=declared_el or (t.selected and t['dolby-vision-el-present']==true)
        end
    end
    if active and (win.paired_el or declared_el or (p7 and dependent)) then dual_seen=true end
    -- Some transport streams expose the paired enhancement layer before the
    -- container reports a Dolby profile. Within the supported DV set, a real
    -- paired EL is the defining Profile 7 signal; keep the UI/Advanced route
    -- truthful instead of leaving a detected FEL/MEL stream as profile 0.
    if profile==0 and active and dual_seen then profile=7;p7=true end
    local off=el_disabled()
    local rpuoff=rpu_disabled()
    local control=native_meta()['comparison-controls']=='yes'
    local canel=active and dual_seen and not rpuoff and (not win.enabled or control)
    local canrpu=profile~=5 and (not win.enabled or native_meta()['rpu-disable-supported']=='yes')
    local bk=tostring(active and dual_seen)..':'..tostring(off)..':'..tostring(win.enabled==true)..':'..tostring(control)..':'..tostring(rpuoff)..':'..profile
    if bk~=button_key then
        button_key=bk
        mp.commandv('script-message-to','uosc','set-button','dv_el',utils.format_json({
            icon=off and 'layers_clear' or 'layers',badge='EL',hide=not (active and dual_seen),active=not off,
            tooltip=rpuoff and 'RPU 已关闭：当前是兼容基础层（BL-only）' or
                not canel and '当前文件没有可独立切换的增强层' or
                off and '增强层画面贡献：关闭 · 点击恢复' or '增强层画面贡献：开启 · 点击关闭',
            command=canel and {'script-message','dv-enhancement-toggle'} or nil,
        }))
    mp.commandv('script-message-to','uosc','set-button','dv_rpu',utils.format_json({
        icon='tune',badge='RPU',hide=not active or profile==0,active=not rpuoff,
        tooltip=profile==5 and 'P5 色彩解释依赖 RPU，不能直接关闭' or
            rpuoff and 'RPU/EL：关闭 · 当前显示兼容基础层 · 点击恢复' or
            'RPU/EL：开启 · 点击切换到兼容基础层（BL-only）',
        command=canrpu and {'script-message','dv-rpu-toggle'} or nil,
    }))
    end
    local rendered=active and not mp.get_property_bool('idle-active',true)
        and mp.get_property_number('time-pos')~=nil
        and mp.get_property('current-vo')=='gpu-next'
    local route=rendered and ((win.enabled and filter_has('windows_dv')) and 'windows'
        or out.colormatrix=='dolbyvision' and 'libplacebo' or rpuoff and 'libplacebo') or ''
    if mp.get_property_native('user-data/dcp',{}).active then route='' end
    local base=src['gamma-original'] or win.source_base_transfer or src.gamma or 'unknown'
    mp.set_property_native('user-data/dv-playback',{
        dual_layer=active and dual_seen,el_disabled=off,route=route,can_toggle_el=canel,can_toggle_rpu=canrpu,rpu_disabled=rpuoff,profile=profile,
        profile_string=profile_string(profile),metadata_carriage=profile==10 and 'av1-emdf' or (profile>0 and 'unspecified-nalu' or ''),
        el_applied=rpuoff and 'off (RPU off)' or native_meta()['el-contribution'],
        rpu_applied=rpuoff and 'off (BL-only)' or native_meta()['rpu-display-contribution'],
        base_transfer=base,render_transfer=out.gamma or 'unknown',
        renderer=mp.get_property('current-vo',''),
        reduced=off or rpuoff,session_only=true,
    })
    local key=route..(off and ':no-el' or '')..(rpuoff and ':no-rpu' or '')
    if route~='' and key~=announced then
        announced=key
        notice(route=='windows' and ((rpuoff and 'Windows HDR → SDR' or 'Windows Dolby → SDR')..(rpuoff and ' · RPU 显示作用关闭' or '')..(off and ' · EL 贡献关闭' or ''))
            or rpuoff and 'gpu-next · RPU/EL 关闭 · 兼容基础层（BL-only）'
            or off and '解 DV · gpu-next / libplacebo · EL 已关闭'
            or '解 DV · gpu-next / libplacebo')
    end
end
local function refresh_paused()
    local at=mp.get_property_number('time-pos')
    if mp.get_property_bool('pause',false) and at then
        mp.commandv('seek',at,'absolute+exact')
    end
end
local function windows_command(which,on)
    local ok,err=mp.commandv('vf-command','windows_dv',which,on and 'yes' or 'no')
    if not ok then mp.osd_message('切换未生效：'..tostring(err),3);return false end
    -- Refresh a paused image through the SAME filter instance; no vf add/remove.
    refresh_paused()
    return true
end
local function enable_el()
    if native_meta()['el-requested']=='off' then windows_command('el',true);update();return true end
    if el_disabled() then
        local ok,err=mp.commandv('vf','remove','@'..label)
        if not ok then mp.osd_message('增强层恢复失败：'..tostring(err),3);return false end
        refresh_paused()
    end
    update()
    return true
end
mp.register_script_message('dv-enhancement-toggle',function()
    update()
    if rpu_disabled() then
        mp.osd_message('RPU 关闭时已经是 BL-only；先恢复 RPU 才能单独比较 EL',3)
        return
    end
    if el_disabled() then enable_el();return end
    if not active or not dual_seen then return end
    if mp.get_property_native('user-data/windows-dv-inwindow',{}).enabled then windows_command('el',false);update();return end
    -- The libplacebo control retains splitter/EL decoding and RPU inheritance;
    -- only the EL image contribution is suppressed. Backend choice is untouched.
    local ok,err=mp.commandv('vf','add','@'..label..':format=enhancement-layer=no')
    if not ok then mp.osd_message('增强层关闭失败：'..tostring(err),3);return end
    refresh_paused()
    update()
end)
mp.register_script_message('dv-rpu-toggle',function()
    update()
    local state=mp.get_property_native('user-data/dv-playback',{})
    if not state.can_toggle_rpu then mp.osd_message('当前格式不能独立关闭 RPU 色彩解释',3);return end
    if mp.get_property_native('user-data/windows-dv-inwindow',{}).enabled then
        windows_command('rpu',rpu_disabled())
    else
        local off=rpu_disabled()
        local ok,err
        if off then
            ok,err=mp.commandv('vf','remove','@'..rpu_label)
        else
            -- A real comparison must change pixels, not merely hide L1/L5.
            -- Restore the saved HDR10/HLG-compatible base representation and
            -- suppress EL reconstruction, whose coefficients depend on RPU.
            ok,err=mp.commandv('vf','add','@'..rpu_label..':format=dolbyvision=no:enhancement-layer=no')
        end
        if not ok then mp.osd_message('RPU 开关失败：'..tostring(err),3);return end
        mp.set_property_bool('gpu-next-dovi-display',true)
        refresh_paused()
    end
    update()
end)
mp.register_event('start-file',function()
    active=false;dual_seen=false;announced=''
    mp.set_property_bool('gpu-next-dovi-display',true)
    if filter_has(rpu_label) then mp.commandv('vf','remove','@'..rpu_label)end
    if native_meta()['rpu-requested']=='off' then mp.commandv('vf-command','windows_dv','rpu','yes')end
    if hide_timer then hide_timer:kill();hide_timer=nil end
    overlay:remove();enable_el();update()
end)
mp.register_event('file-loaded',function()active=true;update()end)
mp.register_event('end-file',function()active=false;overlay:remove();update()end)
mp.observe_property('video-out-params','native',update)
mp.observe_property('user-data/windows-dv-inwindow','native',update)
mp.register_script_message('uosc-version',function()button_key=nil;update()end)
mp.add_periodic_timer(.25,update)
update()
