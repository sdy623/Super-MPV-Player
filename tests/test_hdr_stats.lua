local function run()
 local here=debug.getinfo(1,'S').source:sub(2):match('^(.*)[/\\]')
 local f=assert(io.open(here..'/../src/common/scripts/stats.lua','r'))
 local src=f:read('*a');f:close()
 local body=assert(src:match('(local function append_hdr%(.+)%s+local function append_img_params'))
 local env=setmetatable({o={prefix_sep=' '},format=string.format,append=function(s,v,opt)s[#s+1]=(opt.prefix or '')..tostring(v)end},{__index=_G})
 local fn=assert(loadstring(body..'\nreturn append_hdr'));setfenv(fn,env);local render=fn()
 for _,matrix in ipairs({'dolbyvision','rgb','bt.2020-ncl'}) do
  local s={};render(s,{colormatrix=matrix,['scene-max-r']=1000,['scene-max-g']=900,['scene-max-b']=800,['scene-avg']=100},false)
  local text=table.concat(s,' ')
  assert(not text:find('HDR10+',1,true),'Generic scene luminance was mislabeled HDR10+: '..matrix)
  assert(text:find('Scene luminance:',1,true),'Scene luminance values must remain visible')
  assert(text:find('1000.0 / 900.0 / 800.0',1,true),'Luminance values changed')
 end
 print('PASS: generic luminance does not claim HDR10+; numeric values preserved')
end
local ok,err=pcall(run)
if not ok then mp.msg.error(tostring(err)) end
mp.commandv('quit',ok and '0' or '1')
