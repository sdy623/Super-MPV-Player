@echo off
rem Run in a Visual Studio x64 Native Tools shell. Output stays local.
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /O2 /MT /LD src\windows_dv_frames.cpp /Fe:windows-dv-frames.dll /Fo:windows-dv-frames.obj /link mfplat.lib mf.lib mfuuid.lib mfreadwrite.lib dxgi.lib d3d11.lib dcomp.lib ole32.lib oleaut32.lib user32.lib gdi32.lib psapi.lib
