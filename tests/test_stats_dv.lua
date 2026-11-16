local props={['current-tracks/video']={['dolby-vision-profile']=8,['dolby-vision-level']=9},['user-data/windows-dv-inwindow']={source_dv=true,windows_processed=true,source_base_transfer='hlg',rpu_seen=120,processed_frames=120},['vf-metadata/windows_dv']={['frame-transport']='d3d11-gpu',['gpu-frames']='120'},['video-target-params']={gamma='gamma2.2'}}
local ok,err=pcall(function()
 local f=assert(io.open(assert(arg[1])));local s=f:read('*a');f:close()
 local body=assert(s:match('%-%- BEGIN DV STATUS(.-)%-%- END DV STATUS'),'missing integrated DV section')
 local env={mp={get_property_native=function(k,d)return props[k] or d end},tonumber=tonumber,tostring=tostring,ipairs=ipairs,string=string,table=table}
 local chunk=assert(loadstring(body..'\nreturn dv_status_rows'));setfenv(chunk,env);local rows=chunk()
 local function text()local t={};for _,x in ipairs(rows({colormatrix='dolbyvision'}))do t[#t+1]=x[2]end;return table.concat(t,'\n')end
 local s=text();assert(s:find('P8.4',1,true) and s:find('GPU shared',1,true) and s:find('120',1,true),s)
 props['user-data/windows-dv-inwindow']={source_dv=true,windows_processed=false,enabled=true,state='fallback',fallback_reason='unsupported'};s=text();assert(s:find('unsupported',1,true));assert(not s:find('Windows Dolby active',1,true))
 props['current-tracks/video']={['dolby-vision-profile']=7};props['user-data/windows-dv-inwindow']={source_dv=true,windows_processed=true,paired_el=true,source_base_transfer='pq',processed_frames=5,rpu_seen=5};props['vf-metadata/windows_dv']={};s=text();assert(s:find('P7',1,true) and s:find('paired EL',1,true) and s:find('CPU',1,true));assert(not s:find('MEL',1,true),'No inferred MEL label')
 props['current-tracks/video']={};props['user-data/windows-dv-inwindow']={};local t=rows({gamma='pq'});assert(t[1][2]:find('PQ',1,true));assert(not t[1][2]:find('Dolby Vision',1,true))
end)
assert(ok,err)
print('PASS P8.4 GPU, fallback, P7 paired CPU, PQ-only')
