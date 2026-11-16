-- Explicit CJK runs avoid locale-dependent DirectWrite fallback in Windows UI.
-- Input is already ASS-escaped. Existing tags and explicit/custom fonts stay intact.
local M={}
function M.apply(text,font,platform)
    text=tostring(text)
    if (platform~='windows' and platform~='win32') or font~='Segoe UI' or text:find('\\fn',1,true) then return text end
    local out,at,cjk={},1,false
    while at<=#text do
        local b=text:byte(at)
        if b==123 and (at==1 or text:byte(at-1)~=92) then
            local ending=text:find('}',at,true)
            if ending then out[#out+1]=text:sub(at,ending);at=ending+1
            else out[#out+1]=text:sub(at);break end
        else
            local n=b<128 and 1 or b<224 and 2 or b<240 and 3 or 4
            local cp=b
            if n>1 then
                cp=b%(2^(7-n))
                for j=1,n-1 do cp=cp*64+(text:byte(at+j) or 128)%64 end
            end
            local wanted=(cp>=0x3400 and cp<=0x9fff) or (cp>=0x3000 and cp<=0x303f)
                or (cp>=0xff00 and cp<=0xffef) or (cp>=0x20000 and cp<=0x2fa1f)
            if wanted~=cjk then out[#out+1]=wanted and [[{\fnMicrosoft YaHei UI}]] or [[{\fnSegoe UI}]];cjk=wanted end
            out[#out+1]=text:sub(at,at+n-1);at=at+n
        end
    end
    if cjk then out[#out+1]=[[{\fnSegoe UI}]] end
    return table.concat(out)
end
return M
