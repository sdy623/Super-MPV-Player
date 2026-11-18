-- A thumbnail is decoded from the source, never from the parent's processed RGB.
return function(raw,tracks,meta)
 local profile=tonumber(meta['source-profile'])
 for _,t in ipairs(tracks)do if t.selected and t.type=='video' then profile=tonumber(t['dolby-vision-profile']) or profile end end
 local transfer=raw['gamma-original'] or meta['source-base-transfer'] or raw.gamma
 local hdr=transfer=='hlg' or transfer=='pq'
 return {profile=profile,transfer=profile==5 and 'ipt-pq-c2' or transfer,gpu=profile==5 or profile==7 or profile==8 or hdr,
         preserve_rpu=profile==5,base_only=profile~=5 and hdr}
end
