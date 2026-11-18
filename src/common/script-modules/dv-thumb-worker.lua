-- Explicitly loaded only by thumbfast's disposable gpu-next preview process.
local mp=require 'mp'
local opts={output='',width=160,height=90}
require('mp.options').read_options(opts,'dvthumb')
local generation=0
local function request()
 generation=generation+1;local mine=generation;local tries=0
 local function capture()
  if mine~=generation then return end
  tries=tries+1
  local shot=mp.command_native({'screenshot-raw','video','bgra'})
  if not shot or not shot.data or shot.stride<=0 then
   if tries<15 then mp.add_timeout(.1,capture)end;return
  end
  local rows={};local w,h=opts.width,opts.height
  for y=0,h-1 do
   local sy=math.min(shot.h-1,math.floor(y*shot.h/h));local row={}
   for x=0,w-1 do
    local sx=math.min(shot.w-1,math.floor(x*shot.w/w));local n=sy*shot.stride+sx*4+1
    row[#row+1]=shot.data:sub(n,n+3)
   end
   rows[#rows+1]=table.concat(row)
  end
  local tmp=opts.output..'.writing';local f=io.open(tmp,'wb');if not f then return end
  f:write(table.concat(rows));f:close();os.remove(opts.output);os.rename(tmp,opts.output)
 end
 mp.add_timeout(.08,capture)
end
mp.register_event('playback-restart',request)
mp.register_event('file-loaded',request)
mp.register_event('shutdown',function()os.remove(opts.output..'.writing')end)
