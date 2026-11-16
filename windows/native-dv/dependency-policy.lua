-- SPDX-License-Identifier: MIT
local M={}
function M.windows(dependency,adapter,runtime)
 runtime=runtime or {}
 local available=dependency.installed==true and adapter==true
 local processing=available and (runtime.frames or 0)>0 and (runtime.state=='processed' or runtime.state=='processing')
 return {available=available,processing=processing,state=not dependency.installed and 'dependency-missing' or not adapter and 'adapter-missing' or processing and 'processing' or runtime.state=='fallback' and 'fallback' or 'ready-unverified'}
end
function M.omniphony(engine,bridge)
 return {available=engine==true and bridge==true,state=not engine and 'engine-missing' or not bridge and 'decoder-bridge-missing' or 'ready-unverified'}
end
return M
