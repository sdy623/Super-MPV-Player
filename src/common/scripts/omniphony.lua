-- Optional object-audio rendering; video, timeline and device routing stay with mpv.
local utils=require 'mp.utils'
local root=mp.command_native({'expand-path','~~/plugins/mpv-omniphony/'})
local mode='off'
local saved
local spatialize=false
local studio=false
local function publish()
    mp.set_property_native('user-data/omniphony',{
        mode=mode,spatialize_bed=spatialize,studio=studio,
        decoder_request=mp.get_property('ad',''),
        audio_codec=mp.get_property('audio-codec',''),
        ao=mp.get_property('current-ao',''),
    })
end
local function apply()
    if mode=='off' then
        if saved then
            mp.set_property('ad',saved.ad)
            mp.set_property('audio-spdif',saved.spdif)
            saved=nil
        end
    else
        if not saved then saved={ad=mp.get_property('ad',''),spdif=mp.get_property('audio-spdif','')} end
        mp.set_property('ad','')
        mp.set_property('ad-orender-config',root..mode..'.yaml')
        mp.set_property('ad-orender-bridge-path',table.concat({root..'harletty_dolby_bridge.dll',root..'harletty_dts_bridge.dll',root..'harletty_iamf_bridge.dll'},';'))
        mp.set_property('ad-orender-channel-mode',spatialize and 'spatial' or 'host')
        mp.set_property('ad-orender-osc',studio and 'yes' or 'no')
        mp.set_property('ad-orender-osc-bind','127.0.0.1')
        mp.set_property('audio-spdif','')
        mp.set_property('ad','orender')
    end
    mp.commandv('script-message','omniphony-overlay','disable')
    publish()
end
mp.register_script_message('select',function(value)
    if value~='off' and value~='headphones' and value~='speakers' then return end
    if value~='off' then
        if not mp.get_property_native('option-info/ad-orender-library') then mp.osd_message('当前后端没有 Omniphony；请使用融合核心',5);return end
        for _,f in ipairs({'orender.dll','harletty_dolby_bridge.dll',value..'.yaml'}) do
            if not utils.file_info(root..f) then mp.osd_message('Omniphony 组件缺失：'..f,5);return end
        end
    end
    mode=value;apply()
    local label=mode=='off' and '已恢复原音频解码' or mode=='headphones' and '已选择 Omniphony 耳机渲染；避免叠加 Windows 耳机空间音效' or '已选择 Omniphony 音箱渲染；请核对声道布局'
    mp.osd_message(label,5)
end)
mp.register_script_message('spatialize-bed',function()
    spatialize=not spatialize;apply()
    mp.osd_message(spatialize and '普通声道也交给 Omniphony 空间化' or '普通声道使用原解码；对象音频仍由 Omniphony 处理',4)
end)
mp.register_script_message('studio',function()
    studio=not studio;apply()
    mp.osd_message(studio and '允许本机 Omniphony Studio 连接（端口 9000）' or '已关闭 Studio 连接',4)
end)
mp.register_script_message('status',function()
    publish()
    mp.osd_message('Omniphony：'..mode..'\n音轨：'..mp.get_property('audio-codec','未载入')..'\n输出：'..mp.get_property('current-ao','未就绪'),6)
end)
mp.register_event('file-loaded',publish)
mp.observe_property('audio-codec','string',publish)
mp.observe_property('current-ao','string',publish)
publish()
