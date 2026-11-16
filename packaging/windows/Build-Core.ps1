param([string]$AsioSdk, [string]$BuildDir="build-mpv", [string]$LibplaceboPrefix)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $AsioSdk){$AsioSdk=Join-Path $root 'third_party/asio'}
$options=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'core-build-options.json') -Raw | ConvertFrom-Json
$argsList=@('setup',$BuildDir,(Join-Path $root 'core/mpv'))
foreach($p in $options.PSObject.Properties){
 $value=$p.Value
 if($p.Name -eq 'asio-sdk'){$value=$AsioSdk}
 if($value -is [bool]){$value=$value.ToString().ToLowerInvariant()}
 $argsList+="-D$($p.Name)=$value"
}
if($LibplaceboPrefix){
 $pc=Join-Path ([IO.Path]::GetFullPath($LibplaceboPrefix)) 'lib/pkgconfig'
 if(-not(Test-Path -LiteralPath (Join-Path $pc 'libplacebo.pc'))){throw 'Build/install the patched libplacebo first.'}
 $env:PKG_CONFIG_PATH=$pc+';'+$env:PKG_CONFIG_PATH
}
& meson @argsList
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& ninja -C $BuildDir mpv.exe mpv.com
exit $LASTEXITCODE
