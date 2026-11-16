# Transparent portable launcher. Does not run or restore MPV-Anime.exe.
param([switch]$BluRayMenu,[ValidateSet('Folder','Iso')][string]$OpenBluRay,[switch]$OriginalBackend,[string]$ProbeScript,[string]$LogFile,[string[]]$Media)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$java=Join-Path $root 'runtime\java8'
$data=Join-Path $root 'portable_config\bdj-data'
New-Item -ItemType Directory -Force -Path (Join-Path $data 'cache'),(Join-Path $data 'persistent') | Out-Null
$arguments=@('--idle=yes','--config=yes',('--config-dir='+(Join-Path $root 'portable_config')))
$engineRoot=$root
$retained=Join-Path $root 'original-backend'
if(Test-Path -LiteralPath (Join-Path $retained 'mpv.exe')){$engineRoot=$retained}
# Promote the verified native-window build, while keeping the original engine available.
if(-not $OriginalBackend){
    $native=Join-Path $root 'windows-dv-experimental'
    if((Test-Path -LiteralPath (Join-Path $native 'mpv.exe')) -and (Test-Path -LiteralPath (Join-Path $native 'windows-dv-frames.dll'))){
        $engineRoot=$native
        $arguments+=@('--disc-menu=yes','--gpu-api=d3d11')
        # native-controls.lua enables processing only after official dependency detection.
    }else{Write-Warning 'Verified Windows-DV build missing; using the retained original MPV engine.'}
}
if($OpenBluRay){$arguments+=('--script-opts-append=bd_startup-action='+$OpenBluRay.ToLowerInvariant())}
if($BluRayMenu){$arguments+='--script-opts-append=bd_startup-menu=yes'}
if($ProbeScript){$arguments+="--script=$ProbeScript"}
if($LogFile){$arguments+="--log-file=$LogFile";$arguments+='--msg-level=all=v'}
if($Media){$arguments+='--';$arguments+=$Media}
function Quote-Argument([string]$value){
    $value=[regex]::Replace($value,'(\\*)"','$1$1\"')
    $value=[regex]::Replace($value,'(\\+)$','$1$1')
    return '"'+$value+'"'
}
$psi=New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName=Join-Path $engineRoot 'mpv.exe'
$psi.Arguments=($arguments | ForEach-Object {Quote-Argument $_}) -join ' '
$psi.WorkingDirectory=$engineRoot
$psi.UseShellExecute=$false
$psi.CreateNoWindow=$true
$psi.EnvironmentVariables['JAVA_HOME']=$java
$psi.EnvironmentVariables['LIBBLURAY_CP']=Join-Path $root 'runtime\bdj\libbluray-j2se-1.5.0.jar'
$psi.EnvironmentVariables['LIBBLURAY_CACHE_ROOT']=Join-Path $data 'cache'
$psi.EnvironmentVariables['LIBBLURAY_PERSISTENT_ROOT']=Join-Path $data 'persistent'
# Windows frame-server scratch media stays on D, like the dedicated native launcher.
if($engineRoot -ne $root){
    $scratch=Join-Path $engineRoot 'cache\tmp'
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    $psi.EnvironmentVariables['TEMP']=$scratch
    $psi.EnvironmentVariables['TMP']=$scratch
}
$psi.EnvironmentVariables['PATH']=$engineRoot+';'+(Join-Path $root 'runtime\tools')+';'+(Join-Path $java 'bin')+';'+$psi.EnvironmentVariables['PATH']
[System.Diagnostics.Process]::Start($psi) | Out-Null
