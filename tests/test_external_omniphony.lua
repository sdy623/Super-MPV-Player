local target=arg[1];local messages,props,callbacks,commands={},{ad='lavc', ['audio-spdif']='ac3'}, {},{}
mp={get_property=function(k,d)return props[k] or d end,get_property_native=function(k,d)if k=='option-info/ad-orender-library' then return {} end;return props[k] or d end,set_property=function(k,v)props[k]=v;return true end,set_property_native=function(k,v)props[k]=v end,command_native=function()return 'D:/player/config/plugins/mpv-omniphony/' end,commandv=function(...)commands[#commands+1]={...} end,register_script_message=function(k,v)messages[k]=v end,register_event=function(k,v)callbacks[k]=v end,observe_property=function()end,osd_message=function()end,enable_messages=function()end,add_timeout=function(_,f)f()end}
package.preload['mp.utils']=function()return {file_info=function(path)if path:match('%.yaml$') then return {is_file=true} end end}end
assert(loadfile(target))()
messages.select('headphones');assert(props.ad=='orender','external engine must not require bundled DLL')
assert(props['ad-orender-library']==nil,'leave Studio-first core lookup unchanged');assert(props['ad-orender-bridge-path']==nil,'do not override external bridge discovery')
assert(messages['download-studio'] and messages['download-bridges'],'official download commands required')
callbacks['log-message']({prefix='ad',level='error',text='orender_create failed — decoding natively'})
assert(props.ad=='lavc' and props['audio-spdif']=='ac3','restore prior audio settings when dependencies fail')
messages['download-studio']();assert(commands[#commands][#commands[#commands]]=='https://github.com/mgth/Omniphony/releases/tag/v0.7.0')
messages['download-bridges']();assert(commands[#commands][#commands[#commands]]=='https://github.com/harletty/harletty-bridge/releases/tag/v0.9.0')
print('PASS external components, unchanged engine discovery, failure restoration, official links')
