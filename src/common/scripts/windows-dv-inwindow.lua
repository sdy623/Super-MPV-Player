-- Shared D-candidate diagnostics. Enables Windows processing only in the separate experimental exe.
local utils=require 'mp.utils'
local here=mp.find_config_file('mpv.conf')
local root=here and here:gsub('[/\\]portable_config[/\\]mpv%.conf$','')
local path=root and utils.join_path(root,'windows-dv-experimental/native-controls.lua')
if path and not utils.file_info(path) then path=utils.join_path(root,'native-controls.lua') end
if path and utils.file_info(path) then
 local ok,err=pcall(dofile,path)
 if not ok then mp.msg.error('Windows DV controls: '..tostring(err)) end
end
