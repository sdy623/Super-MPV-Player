-- CPL picture/sound pairing inside the existing mpv window and timeline.
local utils = require 'mp.utils'
local options = require 'mp.options'
local here = utils.split_path(debug.getinfo(1, 'S').source:gsub('^@',''))
local o = {python=mp.command_native({'expand-path','~~/../runtime/anime-nlp/python/python.exe'}),white_mode='preserve'}
options.read_options(o,'dcp')
local current
local file_loaded=false
-- XYZ contains colorimetry, not a mandatory RGB mastering white. Default to
-- a wide D65 working space without a DCI-to-D65 adaptation. No title/CPL rules.
local color_mode=o.white_mode=='cinema' and 'cinema' or 'preserve'
local function apply_picture_path()
    local vf={}
    for _,f in ipairs(mp.get_property_native('vf',{})) do
        if f.label~='windows_dv' and f.label~='dcp_xyz' then vf[#vf+1]=f end
    end
    vf[#vf+1]={name='format',label='dcp_xyz',params={
        primaries=color_mode=='cinema' and 'dci-p3' or 'bt.2020'}}
    mp.set_property_native('file-local-options/vf',vf)
    mp.set_property_bool('file-local-options/icc-profile-auto',true)
    -- Display brightness is not a reason to apply a cinema white adaptation.
    mp.set_property('file-local-options/target-prim','auto')
    mp.set_property('file-local-options/target-trc','auto')
    mp.set_property('file-local-options/target-peak','auto')
end
local function publish()
    if not current then return end
    local data=current
    mp.set_property_native('user-data/dcp',{state=data.standalone and 'xyz' or 'paired',active=true,cpl=data.cpl,cpl_id=data.cpl_id,
        color_mode=color_mode,xyz_white=color_mode=='cinema' and 'DCI adaptation' or 'preserve XYZ',
        picture_path='gpu-next XYZ / factory ICM',title=data.title,duration=data.duration,
        reels=#data.reels,audio_configuration=data.audio_configuration,warnings=data.warnings})
end
local function detect_standalone_xyz()
    if current or not file_loaded then return end
    local par=mp.get_property_native('video-params',{})
    if par.colormatrix~='xyz' or par.gamma~='st428' or
       not mp.get_property('path',''):lower():match('%.mxf$') then return end
    -- Standalone picture has no CPL audio pairing. It still uses exactly the
    -- same color policy and bypasses Windows DV once XYZ is identified.
    current={standalone=true,title=mp.get_property('media-title','DCP XYZ'),
        duration=mp.get_property_native('duration'),reels={},warnings={},audio_configuration=''}
    apply_picture_path();publish()
end
local function color_menu()
    if not current then mp.osd_message('请先打开 DCP 文件夹或 CPL',3);return end
    local data={type='dcp-color',title='DCP 色彩管理',items={
        {title='保持 XYZ 色度（默认）',active=color_mode=='preserve',value={'script-message','dcp-color-mode','preserve'}},
        {title='影院参考白适配（DCI → 屏幕）',active=color_mode=='cinema',value={'script-message','dcp-color-mode','cinema'}},
        {title='出厂 ICM · 浮点处理 · 不走杜比桥',selectable=false,muted=true,value={'ignore'}},
        {title='选择仅本次运行有效；不会修改 DCP',selectable=false,muted=true,value={'ignore'}}}}
    mp.set_property_native('user-data/dcp-menu',data)
    mp.commandv('script-message-to','uosc','open-menu',utils.format_json(data))
end
mp.register_script_message('dcp-color-mode',function(mode)
    if not current or (mode~='preserve' and mode~='cinema') then return end
    color_mode=mode;apply_picture_path();publish();color_menu()
end)
mp.register_script_message('dcp-color-menu',color_menu)
local function local_path(path)
    if path:sub(1,8):lower()=='file:///' then
        return (path:sub(9):gsub('%%(%x%x)',function(h)return string.char(tonumber(h,16))end))
    end
    if path:find('://',1,true) then return nil end
    return path
end
local function candidate(path)
    local p=local_path(path); if not p then return nil end
    local info=utils.file_info(p); if not info then return nil end
    local dir=info.is_dir and p or utils.split_path(p)
    if not info.is_dir and not (p:lower():match('%.xml$') or p:lower():match('assetmap$')) then return nil end
    for _,n in ipairs(utils.readdir(dir,'files') or {}) do
        if n:lower()=='assetmap' or n:lower()=='assetmap.xml' then return p end
    end
end
local function failed(reason)
    mp.msg.error(reason)
    mp.set_property_native('user-data/dcp',{state='error',error=reason})
    mp.osd_message('DCP 无法打开：'..reason,10)
    -- Do not let a broken composition fall through to unpaired MXF playback.
    mp.set_property('stream-open-filename','memory://')
end
mp.add_hook('on_load',4,function(hook)
    current=nil;file_loaded=false
    mp.set_property_native('user-data/dcp',{})
    local path=candidate(mp.get_property('path',''))
    if not path then return end
    hook:defer()
    mp.command_native_async({name='subprocess',args={o.python,'-B',utils.join_path(here,'dcp.py'),path},playback_only=false,capture_stdout=true,capture_stderr=true},function(success,result,async_error)
        local ok,err=pcall(function()
            if not success or not result or result.status~=0 then error('CPL 解析器未能启动：'..tostring(async_error or (result and result.stderr))) end
            local data=utils.parse_json(result.stdout or '')
            if not data or not data.ok then error(data and data.error or 'CPL 返回数据无效') end
            current=data
            mp.set_property('file-local-options/force-media-title',data.title)
            mp.set_property('file-local-options/audio-file-auto','no')
            mp.set_property('file-local-options/autocreate-playlist','no')
            mp.set_property_bool('file-local-options/ytdl',false)
            if data.audio_pan then
                local enabled=mp.set_property_bool("file-local-options/ad-lavc-dcp-pcm-indexed",true)
                if not enabled then error("当前后端没有 DCP PCM 支持，请使用已更新的默认播放器后端") end
                local af=mp.get_property_native('af',{})
                af[#af+1]={name='lavfi',label='dcp-channels',params={graph=data.audio_pan}}
                mp.set_property_native('file-local-options/af',af)
            end
            apply_picture_path()
            mp.set_property('stream-open-filename',data.edl)
            publish()
        end)
        if not ok then current=nil;failed(tostring(err):gsub('^.-:%d+: ','')) end
        hook:cont()
    end)
end)
-- JPEG2000 software decode can stall the player thread. Keep a bounded queue
-- before decoder creation, only for composition/direct MXF JPEG2000 playback.
mp.add_hook('on_preloaded',4,function()
    local path=local_path(mp.get_property('path',''))
    if not current and not (path and path:lower():match('%.mxf$')) then return end
    for _,track in ipairs(mp.get_property_native('track-list',{})) do
        if track.type=='video' and track.codec=='jpeg2000' then
            mp.set_property_bool('file-local-options/vd-queue-enable',true)
            mp.set_property('file-local-options/vd-queue-max-bytes','256MiB')
            mp.set_property_native('file-local-options/vd-queue-max-samples',8)
            mp.set_property_native('file-local-options/vd-queue-max-secs',0.35)
            return
        end
    end
end)
mp.register_event('file-loaded',function()
    file_loaded=true;detect_standalone_xyz()
    if not current or current.standalone then return end
    mp.set_property_native('chapter-list',current.chapters)
    if #current.warnings>0 then mp.osd_message(table.concat(current.warnings,'\n'),8) end
end)




mp.register_event('end-file',function(e)
    if e.reason=='redirect' then return end
    current=nil;file_loaded=false;mp.set_property_native('user-data/dcp',{})
    mp.set_property_native('user-data/dcp-menu',{})
end)

mp.observe_property('video-params','native',detect_standalone_xyz)
