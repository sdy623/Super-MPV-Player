-- DCP composition importer, kept independent of theme and BD scripts.
local utils=require 'mp.utils'
local source=utils.split_path(debug.getinfo(1,'S').source:gsub('^@',''))
mp.commandv('load-script',utils.join_path(source,'../plugins/mpv-dcp/main.lua'))
