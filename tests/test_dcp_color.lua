local root=arg[1]
local data={ok=true,cpl_id='urn:uuid:unknown-composition',cpl='D:/test/CPL.xml',title='Unseen DCP',duration=60,reels={{}},chapters={{time=0,title='Reel 1'}},warnings={},edl='edl://picture;!new_stream;sound',audio_configuration='51/L,R,C,LFE,Ls,Rs,-,-',audio_pan='pan=5.1(side)|FL=c0|FR=c1|FC=c2|LFE=c3|SL=c4|SR=c5'}
local props={path='D:/test',vf={{name='windows-dv',label='windows_dv'},{name='format',label='user_filter'}},af={},['icc-profile-auto']=false}
local messages={};local events={};local hooks={}
mp={msg={error=print,info=function()end,warn=print},osd_message=function()end}
function mp.get_property(k,def)return props[k]or def end
function mp.get_property_native(k,def)return props[k]or def end
function mp.set_property(k,v)props[k]=v;return true end
mp.set_property_native=mp.set_property;mp.set_property_bool=mp.set_property
function mp.command_native(t)return root..'/portable_config' end
function mp.commandv(...)end
function mp.add_hook(n,p,fn)hooks[n]=fn end
function mp.command_native_async(t,cb)cb(true,{status=0,stdout='RESULT'})end
function mp.register_event(n,fn)events[n]=fn end
function mp.register_script_message(n,fn)messages[n]=fn end
function mp.add_key_binding(...)end
function mp.observe_property(...)end
local utils={split_path=function(p)return p:match('^(.*[/\\])')or '', '' end,join_path=function(a,b)return a..'/'..b end,file_info=function()return{is_dir=true}end,readdir=function()return{'ASSETMAP'}end,parse_json=function(s)return s=='RESULT'and data or {}end,format_json=function()return'{}'end}
package.preload['mp.utils']=function()return utils end
package.preload['mp.options']=function()return{read_options=function()end}end
dofile(root..'/src/common/plugins/mpv-dcp/main.lua')
hooks.on_load({defer=function()end,cont=function()end});events['file-loaded']()
local vf=props['file-local-options/vf'];assert(#vf==2 and vf[1].label=='user_filter')
assert(vf[2].label=='dcp_xyz'and vf[2].params.primaries=='bt.2020','unseen DCP must default to XYZ/D65 wide working space')
assert(props['file-local-options/icc-profile-auto']==true,'DCP must retain factory ICM')
assert(props['user-data/dcp'].color_mode=='preserve')
messages['dcp-color-mode']('cinema');assert(props['file-local-options/vf'][2].params.primaries=='dci-p3')
messages['dcp-color-mode']('preserve');assert(props['file-local-options/vf'][2].params.primaries=='bt.2020')
local before=props['file-local-options/vf'];messages['dcp-color-mode']('invalid');assert(props['file-local-options/vf']==before)
assert(props.vf[1].label=='windows_dv','file-local changes must not mutate global filter array')
print('PASS generic CPL, both modes, ICM, unrelated filter and global-state preservation')

assert(events["end-file"], "DCP state must clear on stop/EOF")
events["end-file"]({reason="eof"});assert(props["user-data/dcp"].state==nil)

props.path='D:/test/picture.mxf';props['video-params']={colormatrix='xyz',gamma='st428'};props['file-local-options/vf']=nil
events['file-loaded']();assert(props['user-data/dcp'].state=='xyz','standalone DCP MXF must use same isolated color path')

assert(hooks.on_preloaded,'JPEG2000 queue must be configured before decoder creation')
props['track-list']={{type='video',codec='jpeg2000'}}
hooks.on_preloaded()
assert(props['file-local-options/vd-queue-enable']==true)
assert(props['file-local-options/vd-queue-max-bytes']=='256MiB')
assert(props['file-local-options/vd-queue-max-samples']==8)
assert(props['file-local-options/vd-queue-max-secs']==0.35)
events['end-file']({reason='eof'})
props.path='D:/test/video.mkv';props['track-list']={{type='video',codec='hevc'}}
props['file-local-options/vd-queue-enable']=nil
hooks.on_preloaded();assert(props['file-local-options/vd-queue-enable']==nil,'ordinary playback must retain its queue settings')
props.path='D:/test/not-j2k.mxf';hooks.on_preloaded();assert(props['file-local-options/vd-queue-enable']==nil)
print('PASS bounded JPEG2000 queue and ordinary-file isolation')
