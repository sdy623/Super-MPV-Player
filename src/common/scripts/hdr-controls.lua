-- HDR/DV diagnostics and session-local gpu-next controls.
-- Native playback is an optional separate MediaEngine window. Never label PQ as DV.
local utils=require 'mp.utils'
local mode='display-auto'
local overrides={}
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
    local ok,err=mp.set_property(key,tostring(value))
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
        vo=mp.get_property('current-vo','未初始化'),options=options,session_only=true}
    mp.set_property_native('user-data/hdr-controls',result)
    indicator(hdr,dv)
    return result
end
local function apply()
    timer=nil
    if mp.get_property_native('user-data/dcp',{}).active==true then
        mp.set_property_bool('file-local-options/icc-profile-auto',true)
        snapshot();return
    end
    local inside=mp.get_property_native('user-data/windows-dv-inwindow',{})
    if inside.windows_processed and not inside.float_output then
        -- The frame processor returns device-referred SDR RGB with the system ICC.
        -- Keep its matching target profile; do not map that SDR output to HDR again.
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','bt.1886');set('target-prim','bt.709');set('target-peak','203')
        set('icc-profile-auto','yes')
    elseif mode=='sdr-reference' then
        local src=mp.get_property_native('video-params',{})
        local hdr=src.gamma=='pq' or src.gamma=='hlg' or src.colormatrix=='dolbyvision'
        set('target-colorspace-hint',hdr and 'yes' or 'auto')
        set('target-colorspace-hint-mode','target')
        set('target-trc',hdr and 'bt.1886' or 'auto')
        set('target-prim',hdr and 'bt.709' or 'auto')
        set('target-peak',hdr and '203' or 'auto')
        set('icc-profile-auto','yes')
    elseif mode=='display-auto' then
        set('target-colorspace-hint','yes');set('target-colorspace-hint-mode','target')
        set('target-trc','auto');set('target-prim','auto');set('target-peak','auto')
        -- Do not apply the SDR factory ICM over an HDR output target.
        set('icc-profile-auto','no')
    end
    for key,value in pairs(overrides) do set(key,value) end
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
        {title='SDR 映射 + 系统 ICC（203 nits 参考值）',active=mode=='sdr-reference',value={'script-message-to','hdr_controls','mode','sdr-reference'}},
        {title='跟随显示器：自动 SDR / HDR10（不是原生 DV）',active=mode=='display-auto',value={'script-message-to','hdr_controls','mode','display-auto'}}}
    for _,key in ipairs(order) do
        local sub={}
        for _,value in ipairs(fields[key]) do
            local actual=mp.get_property('options/'..key,'')
            local selected=actual==value or (tonumber(actual) and tonumber(actual)==tonumber(value))
            sub[#sub+1]={title=value,active=selected,value={'script-message-to','hdr_controls','set',key,value}}
        end
        local fixed_by_windows=inside.windows_processed and not inside.float_output and (key=='tone-mapping' or key=='gamut-mapping-mode' or key:match('^hdr%-') or key=='tone-mapping-visualize')
        items[#items+1]={id=key,title=labels[key],hint=fixed_by_windows and '先切回原映射再调整' or mp.get_property('options/'..key,''),selectable=not fixed_by_windows,muted=fixed_by_windows,items=sub}
    end
    items[#items+1]={title='重置：跟随显示器默认值',value={'script-message-to','hdr_controls','reset'}}
    items[#items+1]={title='参数仅本次运行有效；不改原盘、不转码',selectable=false,muted=true,value={'ignore'}}
    local data={type='hdr-controls',title='HDR / Dolby Vision / 色调映射',items=items}
    mp.set_property_native('user-data/hdr-menu',data)
    mp.commandv('script-message-to','uosc','open-menu',utils.format_json(data))
end
mp.register_script_message('menu',menu)
mp.register_script_message('set',function(key,value)
    for _,allowed in ipairs(fields[key] or {}) do
        if value==allowed then overrides[key]=value;apply();menu();return end
    end
    mp.msg.warn('Rejected unsupported HDR option/value')
end)
mp.register_script_message('mode',function(value)
    if value~='sdr-reference' and value~='display-auto' then return end
    mode=value;overrides['target-peak']=nil;overrides['icc-profile-auto']=nil
    snapshot();schedule();mp.add_timeout(0.12,menu)
end)
mp.register_script_message('reset',function()
    overrides={};mode='display-auto'
    for key,value in pairs(defaults) do if value then set(key,value) end end
    snapshot();schedule();mp.add_timeout(0.12,menu)
end)
local processed_before=nil
mp.observe_property('user-data/windows-dv-inwindow','native',function(_,v)
    local active=type(v)=='table' and (tostring(v.windows_processed==true)..':'..tostring(v.float_output==true)) or 'false:false'
    if active~=processed_before then processed_before=active;schedule() end
    snapshot()
end)
mp.observe_property('user-data/dcp','native',schedule)
mp.observe_property('video-params','native',schedule)
mp.observe_property('video-out-params','native',schedule)
mp.register_event('file-loaded',function()media_active=true;snapshot();schedule()end)
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
