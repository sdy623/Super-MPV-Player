-- CPL picture/sound pairing inside the existing mpv window and timeline.
local utils = require 'mp.utils'
local options = require 'mp.options'
local here = utils.split_path(debug.getinfo(1, 'S').source:gsub('^@',''))
local o = {python=mp.command_native({'expand-path','~~/../runtime/anime-nlp/python/python.exe'})}
options.read_options(o,'dcp')
local current
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
    current=nil
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
            mp.set_property('stream-open-filename',data.edl)
            mp.set_property_native('user-data/dcp',{state='paired',cpl=data.cpl,title=data.title,duration=data.duration,reels=#data.reels,audio_configuration=data.audio_configuration,warnings=data.warnings})
        end)
        if not ok then current=nil;failed(tostring(err):gsub('^.-:%d+: ','')) end
        hook:cont()
    end)
end)
mp.register_event('file-loaded',function()
    if not current then return end
    mp.set_property_native('chapter-list',current.chapters)
    if #current.warnings>0 then mp.osd_message(table.concat(current.warnings,'\n'),8) end
end)
