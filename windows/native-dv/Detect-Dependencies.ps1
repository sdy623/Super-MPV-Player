# SPDX-License-Identifier: MIT
# Read-only. Package presence is not proof of license entitlement or rendering.
param([string]$DisplayName='')
$ErrorActionPreference='Stop'

function Read-BigEndianUInt32([byte[]]$Bytes,[int]$Offset) {
 [uint64]$value=([uint64]$Bytes[$Offset] -shl 24) -bor ([uint64]$Bytes[$Offset+1] -shl 16) -bor ([uint64]$Bytes[$Offset+2] -shl 8) -bor $Bytes[$Offset+3]
 return [uint32]$value
}
function Read-S15Fixed16([byte[]]$Bytes,[int]$Offset) {
 [int64]$raw=Read-BigEndianUInt32 $Bytes $Offset
 if($raw -ge 2147483648){$raw-=4294967296}
 return [double]$raw/65536.0
}
function Read-IccXyzTag([byte[]]$Bytes,$Tag) {
 if(-not $Tag -or $Tag.offset+20 -gt $Bytes.Length){return $null}
 if([Text.Encoding]::ASCII.GetString($Bytes,$Tag.offset,4) -ne 'XYZ '){return $null}
 [ordered]@{
  x=[math]::Round((Read-S15Fixed16 $Bytes ($Tag.offset+8)),8)
  y=[math]::Round((Read-S15Fixed16 $Bytes ($Tag.offset+12)),8)
  z=[math]::Round((Read-S15Fixed16 $Bytes ($Tag.offset+16)),8)
 }
}
function Read-IccDisplayTarget([string]$Path) {
 if(-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
 try {
  [byte[]]$bytes=[IO.File]::ReadAllBytes($Path)
  if($bytes.Length -lt 132){return $null}
  $count=[int](Read-BigEndianUInt32 $bytes 128)
  $tags=@{}
  for($i=0;$i -lt $count;$i++){
   $entry=132+12*$i
   if($entry+12 -gt $bytes.Length){break}
   $signature=[Text.Encoding]::ASCII.GetString($bytes,$entry,4)
   $offset=[int](Read-BigEndianUInt32 $bytes ($entry+4))
   $size=[int](Read-BigEndianUInt32 $bytes ($entry+8))
   if($offset -ge 0 -and $size -ge 8 -and $offset+$size -le $bytes.Length){$tags[$signature]=[ordered]@{offset=$offset;size=$size}}
  }
  $lumi=if($tags.ContainsKey('lumi')){Read-IccXyzTag $bytes $tags['lumi']}else{$null}
  $white=if($tags.ContainsKey('wtpt')){Read-IccXyzTag $bytes $tags['wtpt']}else{$null}
  $black=if($tags.ContainsKey('bkpt')){Read-IccXyzTag $bytes $tags['bkpt']}else{$null}
  $lumiY=if($lumi){$lumi.y}else{0.0}
  $whiteY=if($white){$white.y}else{1.0}
  $blackY=if($black){$black.y}else{0.0}
  $relativeBlack=if($blackY -gt 0){$blackY/$whiteY}else{0.0}
  $contrast=if($relativeBlack -gt 0){1.0/$relativeBlack}else{0.0}
  $validLuminance=$lumiY -ge 40 -and $lumiY -le 10000 -and $whiteY -gt 0
  $ascii=[Text.Encoding]::ASCII.GetString($bytes)
  $unicode=[Text.Encoding]::Unicode.GetString($bytes)
  $dolbyModes=@('Dolby Vision Dark','Dolby Vision Bright','Dolby Vision Vivid') | Where-Object {$ascii.Contains($_) -or $unicode.Contains($_)}
  [ordered]@{
   peak_nits=$(if($validLuminance){[math]::Round($lumiY,6)}else{$null})
   black_nits=$(if($validLuminance -and $contrast -gt 0){[math]::Round($lumiY/$contrast,6)}else{$null})
   contrast=$(if($contrast -ge 100 -and $contrast -le 1000000){[math]::Round($contrast,6)}else{$null})
   white_point_xyz=$white
   black_point_xyz=$black
   colorants_xyz=[ordered]@{
    red=$(if($tags.ContainsKey('rXYZ')){Read-IccXyzTag $bytes $tags['rXYZ']}else{$null})
    green=$(if($tags.ContainsKey('gXYZ')){Read-IccXyzTag $bytes $tags['gXYZ']}else{$null})
    blue=$(if($tags.ContainsKey('bXYZ')){Read-IccXyzTag $bytes $tags['bXYZ']}else{$null})
   }
   has_mhc2=$tags.ContainsKey('MHC2')
   has_dvb1=$tags.ContainsKey('DVB1')
   dolby_modes=@($dolbyModes)
   tag_signatures=@($tags.Keys | Sort-Object)
   source=$(if($validLuminance -and $black){'icc-lumi-bkpt'}elseif($validLuminance){'icc-lumi'}else{'icc-profile-metadata'})
  }
 } catch { return $null }
}
function Convert-HalfToDouble([uint16]$Bits) {
 $sign=if(($Bits -band 0x8000) -ne 0){-1.0}else{1.0}
 $exponent=($Bits -shr 10) -band 0x1f
 $fraction=$Bits -band 0x03ff
 if($exponent -eq 0){return $sign*[math]::Pow(2,-14)*($fraction/1024.0)}
 if($exponent -eq 31){return [double]::NaN}
 return $sign*[math]::Pow(2,$exponent-15)*(1.0+$fraction/1024.0)
}
function Read-ActiveDisplayIdTarget([string]$MonitorId) {
 try {
  if(-not $MonitorId){
   $active=Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorID | Where-Object {$_.Active} | Select-Object -First 1
   if(-not $active){$active=Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorConnectionParams | Where-Object {$_.Active} | Select-Object -First 1}
   $MonitorId=$active.InstanceName
  }
  if(-not $MonitorId){return $null}
  $parts=([string]$MonitorId -replace '^MONITOR\\','DISPLAY\') -split '\\'
  if($parts.Count -lt 2){return $null}
  if($parts.Count -ge 3){
   $instance=$parts[2] -replace '_\d+$',''
   $key="HKLM:\SYSTEM\CurrentControlSet\Enum\DISPLAY\$($parts[1])\$instance\Device Parameters"
  } else {
   $base="HKLM:\SYSTEM\CurrentControlSet\Enum\DISPLAY\$($parts[1])"
   $key=Get-ChildItem -LiteralPath $base -ErrorAction Stop | ForEach-Object {Join-Path $_.PSPath 'Device Parameters'} | Where-Object {(Get-ItemProperty -LiteralPath $_ -ErrorAction SilentlyContinue).EDID} | Select-Object -First 1
   if(-not $key){return $null}
  }
  [byte[]]$edid=(Get-ItemProperty -LiteralPath $key -ErrorAction Stop).EDID
  for($base=128;$base+127 -lt $edid.Length;$base+=128){
   if($edid[$base] -ne 0x70 -or $edid[$base+1] -lt 0x20){continue}
   $sectionEnd=[math]::Min($base+5+$edid[$base+2],$base+127)
   for($pos=$base+5;$pos+3 -le $sectionEnd;){
    $tag=$edid[$pos];$length=[int]$edid[$pos+2];$payload=$pos+3
    if($payload+$length -gt $sectionEnd){break}
    if($tag -eq 0x21 -and $length -ge 29){
     $full=Convert-HalfToDouble ([uint16](([int]$edid[$payload+21]) -bor (([int]$edid[$payload+22]) -shl 8)))
     $ten=Convert-HalfToDouble ([uint16](([int]$edid[$payload+23]) -bor (([int]$edid[$payload+24]) -shl 8)))
     $minimum=Convert-HalfToDouble ([uint16](([int]$edid[$payload+25]) -bor (([int]$edid[$payload+26]) -shl 8)))
     return [ordered]@{peak_full_nits=$full;peak_10pct_nits=$ten;min_nits=$minimum;source='displayid-2.0-block-21'}
    }
    $pos=$payload+$length
   }
  }
 } catch { return $null }
 return $null
}
$screenCount=0
try { Add-Type -AssemblyName System.Windows.Forms; $screenCount=@([System.Windows.Forms.Screen]::AllScreens).Count } catch {}
$profile='';$monitorId=''
try {
 Add-Type -TypeDefinition @"
using System;
using System.Globalization;
using System.Text;
using System.Runtime.InteropServices;
public static class MpcDisplayProfile {
 [StructLayout(LayoutKind.Sequential)] struct LUID {public uint LowPart;public int HighPart;}
 [StructLayout(LayoutKind.Sequential)] struct PathSource {public LUID adapterId;public uint id,modeInfoIdx,statusFlags;}
 [StructLayout(LayoutKind.Sequential)] struct Rational {public uint Numerator,Denominator;}
 [StructLayout(LayoutKind.Sequential)] struct PathTarget {public LUID adapterId;public uint id,modeInfoIdx;public int outputTechnology,rotation,scaling;public Rational refreshRate;public int scanLineOrdering;[MarshalAs(UnmanagedType.Bool)]public bool targetAvailable;public uint statusFlags;}
 [StructLayout(LayoutKind.Sequential)] struct PathInfo {public PathSource sourceInfo;public PathTarget targetInfo;public uint flags;}
 [StructLayout(LayoutKind.Explicit,Size=64)] struct ModeInfo {[FieldOffset(0)]public uint infoType;[FieldOffset(4)]public uint id;[FieldOffset(8)]public LUID adapterId;}
 [StructLayout(LayoutKind.Sequential)] struct DeviceHeader {public uint type,size;public LUID adapterId;public uint id;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct SourceName {public DeviceHeader header;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string viewGdiDeviceName;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct TargetName {public DeviceHeader header;public uint flags;public int outputTechnology;public ushort edidManufactureId,edidProductCodeId;public uint connectorInstance;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=64)]public string monitorFriendlyDeviceName;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string monitorDevicePath;}
 [StructLayout(LayoutKind.Sequential)] struct DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO {public DeviceHeader header;public uint value;public int colorEncoding;public uint bitsPerColorChannel;}
 [StructLayout(LayoutKind.Sequential)] struct DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 {public DeviceHeader header;public uint value;public int colorEncoding;public uint bitsPerColorChannel;public int activeColorMode;}
 [StructLayout(LayoutKind.Sequential)] struct RECT {public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct DXGI_OUTPUT_DESC1 {
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string DeviceName;
  public RECT DesktopCoordinates;
  [MarshalAs(UnmanagedType.Bool)] public bool AttachedToDesktop;
  public int Rotation;
  public IntPtr Monitor;
  public uint BitsPerColor;
  public int ColorSpace;
  public float RedX,RedY,GreenX,GreenY,BlueX,BlueY,WhiteX,WhiteY;
  public float MinLuminance,MaxLuminance,MaxFullFrameLuminance;
 }
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int EnumAdapters1Delegate(IntPtr self,uint index,out IntPtr adapter);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int EnumOutputsDelegate(IntPtr self,uint index,out IntPtr output);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int QueryInterfaceDelegate(IntPtr self,ref Guid iid,out IntPtr result);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int GetDesc1Delegate(IntPtr self,out DXGI_OUTPUT_DESC1 desc);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate uint ReleaseDelegate(IntPtr self);
 [DllImport("dxgi.dll",CallingConvention=CallingConvention.StdCall)] static extern int CreateDXGIFactory1(ref Guid iid,out IntPtr factory);
 [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr w);
 [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr w, IntPtr dc);
 [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint flags,out uint paths,out uint modes);
 [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint flags,ref uint paths,[Out]PathInfo[] pathArray,ref uint modes,[Out]ModeInfo[] modeArray,IntPtr topology);
 [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref SourceName packet);
 [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref TargetName packet);
 [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO packet);
 [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 packet);
 [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] static extern IntPtr CreateDC(string driver, string device, string output, IntPtr initData);
 [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
 [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] static extern bool GetICMProfile(IntPtr dc, ref uint n, StringBuilder name);
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct DISPLAY_DEVICE {
  public int cb; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string DeviceName;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceString;
  public int StateFlags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceID;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceKey;
 }
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool EnumDisplayDevices(string device, uint number, ref DISPLAY_DEVICE display, uint flags);
 static Delegate Vtable(IntPtr obj,int index,Type type) {
  IntPtr table=Marshal.ReadIntPtr(obj);
  return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(table,index*IntPtr.Size),type);
 }
 static void ReleaseCom(IntPtr obj) {
  if(obj!=IntPtr.Zero)((ReleaseDelegate)Vtable(obj,2,typeof(ReleaseDelegate)))(obj);
 }
 public static string ReadDxgiOutput(string displayName) {
  if(String.IsNullOrEmpty(displayName))return "";
  Guid factoryIid=new Guid("770aae78-f26f-4dba-a829-253c83d1b387");
  Guid output6Iid=new Guid("068346e8-aaec-4b84-add7-137f513f77a1");
  IntPtr factory=IntPtr.Zero;
  if(CreateDXGIFactory1(ref factoryIid,out factory)<0||factory==IntPtr.Zero)return "";
  try {
   var enumAdapters=(EnumAdapters1Delegate)Vtable(factory,12,typeof(EnumAdapters1Delegate));
   for(uint ai=0;ai<32;ai++){
    IntPtr adapter=IntPtr.Zero;
    if(enumAdapters(factory,ai,out adapter)<0||adapter==IntPtr.Zero)break;
    try {
     var enumOutputs=(EnumOutputsDelegate)Vtable(adapter,7,typeof(EnumOutputsDelegate));
     for(uint oi=0;oi<32;oi++){
      IntPtr output=IntPtr.Zero;
      if(enumOutputs(adapter,oi,out output)<0||output==IntPtr.Zero)break;
      try {
       IntPtr output6=IntPtr.Zero;
       var query=(QueryInterfaceDelegate)Vtable(output,0,typeof(QueryInterfaceDelegate));
       if(query(output,ref output6Iid,out output6)<0||output6==IntPtr.Zero)continue;
       try {
        DXGI_OUTPUT_DESC1 d;
        var getDesc=(GetDesc1Delegate)Vtable(output6,27,typeof(GetDesc1Delegate));
        if(getDesc(output6,out d)<0||!String.Equals(d.DeviceName,displayName,StringComparison.OrdinalIgnoreCase))continue;
        string[] fields={d.DeviceName,d.MinLuminance.ToString("R",CultureInfo.InvariantCulture),
         d.MaxLuminance.ToString("R",CultureInfo.InvariantCulture),d.MaxFullFrameLuminance.ToString("R",CultureInfo.InvariantCulture),
         d.BitsPerColor.ToString(CultureInfo.InvariantCulture),d.ColorSpace.ToString(CultureInfo.InvariantCulture),
         d.RedX.ToString("R",CultureInfo.InvariantCulture),d.RedY.ToString("R",CultureInfo.InvariantCulture),
         d.GreenX.ToString("R",CultureInfo.InvariantCulture),d.GreenY.ToString("R",CultureInfo.InvariantCulture),
         d.BlueX.ToString("R",CultureInfo.InvariantCulture),d.BlueY.ToString("R",CultureInfo.InvariantCulture),
         d.WhiteX.ToString("R",CultureInfo.InvariantCulture),d.WhiteY.ToString("R",CultureInfo.InvariantCulture)};
        return String.Join(";",fields);
       } finally {ReleaseCom(output6);}
      } finally {ReleaseCom(output);}
     }
    } finally {ReleaseCom(adapter);}
   }
  } finally {ReleaseCom(factory);}
  return "";
 }
 public static string Read(string displayName) {
  IntPtr dc=String.IsNullOrEmpty(displayName)?GetDC(IntPtr.Zero):CreateDC("DISPLAY", displayName,null,IntPtr.Zero);
  if(dc==IntPtr.Zero)return "";
  try {uint n=32768;var b=new StringBuilder((int)n);return GetICMProfile(dc,ref n,b)?b.ToString():"";}
  finally {if(String.IsNullOrEmpty(displayName))ReleaseDC(IntPtr.Zero,dc);else DeleteDC(dc);}
 }
 public static string ReadMonitorId(string displayName) {
  if(String.IsNullOrEmpty(displayName))return "";
  uint pathCount,modeCount;
  if(GetDisplayConfigBufferSizes(2,out pathCount,out modeCount)==0){
   var paths=new PathInfo[pathCount];var modes=new ModeInfo[modeCount];
   if(QueryDisplayConfig(2,ref pathCount,paths,ref modeCount,modes,IntPtr.Zero)==0){
    for(int i=0;i<pathCount;i++){
     var source=new SourceName();source.header.type=1;source.header.size=(uint)Marshal.SizeOf(source);source.header.adapterId=paths[i].sourceInfo.adapterId;source.header.id=paths[i].sourceInfo.id;
     if(DisplayConfigGetDeviceInfo(ref source)!=0||!String.Equals(source.viewGdiDeviceName,displayName,StringComparison.OrdinalIgnoreCase))continue;
     var target=new TargetName();target.header.type=2;target.header.size=(uint)Marshal.SizeOf(target);target.header.adapterId=paths[i].targetInfo.adapterId;target.header.id=paths[i].targetInfo.id;
     if(DisplayConfigGetDeviceInfo(ref target)!=0)return "";
     ushort packed=(ushort)((target.edidManufactureId>>8)|(target.edidManufactureId<<8));
     char a=(char)('A'+((packed>>10)&31)-1),b=(char)('A'+((packed>>5)&31)-1),c=(char)('A'+(packed&31)-1);
     if(a<'A'||b<'A'||c<'A')return "";
     return String.Format("DISPLAY\\{0}{1}{2}{3:X4}",a,b,c,target.edidProductCodeId);
    }
   }
  }
  var direct=new DISPLAY_DEVICE();direct.cb=Marshal.SizeOf(direct);
  if(EnumDisplayDevices(displayName,0,ref direct,0) && !String.IsNullOrEmpty(direct.DeviceID))return direct.DeviceID;
  for(uint i=0;i<32;i++){var adapter=new DISPLAY_DEVICE();adapter.cb=Marshal.SizeOf(adapter);
   if(!EnumDisplayDevices(null,i,ref adapter,1))break;
   if(!String.Equals(adapter.DeviceName,displayName,StringComparison.OrdinalIgnoreCase))continue;
   var monitor=new DISPLAY_DEVICE();monitor.cb=Marshal.SizeOf(monitor);
   return EnumDisplayDevices(adapter.DeviceName,0,ref monitor,0)?monitor.DeviceID:"";
  }
  return "";
 }
 public static string ReadAdvancedColor(string displayName) {
  if(String.IsNullOrEmpty(displayName))return "";
  uint pathCount,modeCount;
  if(GetDisplayConfigBufferSizes(2,out pathCount,out modeCount)!=0)return "";
  var paths=new PathInfo[pathCount];var modes=new ModeInfo[modeCount];
  if(QueryDisplayConfig(2,ref pathCount,paths,ref modeCount,modes,IntPtr.Zero)!=0)return "";
  for(int i=0;i<pathCount;i++){
   var source=new SourceName();source.header.type=1;source.header.size=(uint)Marshal.SizeOf(source);source.header.adapterId=paths[i].sourceInfo.adapterId;source.header.id=paths[i].sourceInfo.id;
   if(DisplayConfigGetDeviceInfo(ref source)!=0||!String.Equals(source.viewGdiDeviceName,displayName,StringComparison.OrdinalIgnoreCase))continue;
   var info2=new DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2();info2.header.type=15;info2.header.size=(uint)Marshal.SizeOf(info2);info2.header.adapterId=paths[i].targetInfo.adapterId;info2.header.id=paths[i].targetInfo.id;
   if(DisplayConfigGetDeviceInfo(ref info2)==0){
    bool supported=(info2.value&1)!=0,active=(info2.value&2)!=0,hdrSupported=(info2.value&16)!=0,hdrUserEnabled=(info2.value&32)!=0;
    string mode=info2.activeColorMode==2?"hdr":(info2.activeColorMode==1?"wcg":"sdr");
    return String.Format("2;{0};{1};{2};{3};{4};{5};{6}",mode,hdrSupported,hdrUserEnabled,active,info2.bitsPerColorChannel,info2.colorEncoding,supported);
   }
   var info=new DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO();info.header.type=9;info.header.size=(uint)Marshal.SizeOf(info);info.header.adapterId=paths[i].targetInfo.adapterId;info.header.id=paths[i].targetInfo.id;
   if(DisplayConfigGetDeviceInfo(ref info)==0){
    bool supported=(info.value&1)!=0,enabled=(info.value&2)!=0;
    return String.Format("1;{0};{1};{2};{3};{4};{5};{6}",enabled?"hdr":"sdr",supported,enabled,enabled,info.bitsPerColorChannel,info.colorEncoding,supported);
   }
   return "";
  }
  return "";
 }
}
"@
 $profile=[MpcDisplayProfile]::Read($DisplayName)
 $monitorId=[MpcDisplayProfile]::ReadMonitorId($DisplayName)
 $advancedColor=[MpcDisplayProfile]::ReadAdvancedColor($DisplayName)
 $dxgiOutput=[MpcDisplayProfile]::ReadDxgiOutput($DisplayName)
 if(-not (Test-Path -LiteralPath $profile -PathType Leaf)){$profile=''}
} catch { $profile='';$dxgiOutput='' }
$displayColorMode='unknown';$displayHdrSupported=$null;$displayHdrUserEnabled=$null;$displayHdrActive=$null;$displayBitsPerColor=$null;$displayColorEncoding=$null;$advancedColorVersion=$null
if($advancedColor){
 $parts=@($advancedColor -split ';')
 if($parts.Count -ge 8){
  $advancedColorVersion=[int]$parts[0];$displayColorMode=$parts[1]
  $displayHdrSupported=[bool]::Parse($parts[2]);$displayHdrUserEnabled=[bool]::Parse($parts[3]);$displayHdrActive=$displayColorMode -eq 'hdr'
  $displayBitsPerColor=[int]$parts[5];$displayColorEncoding=[int]$parts[6]
 }
}
$iccTarget=Read-IccDisplayTarget $profile
$edidTarget=Read-ActiveDisplayIdTarget $monitorId
$dxgiTarget=$null
if($dxgiOutput){
 $parts=@($dxgiOutput -split ';')
 if($parts.Count -ge 14){
  $culture=[Globalization.CultureInfo]::InvariantCulture
  $dxgiTarget=[ordered]@{
   device_name=$parts[0]
   min_luminance_nits=[double]::Parse($parts[1],$culture)
   max_luminance_nits=[double]::Parse($parts[2],$culture)
   max_full_frame_luminance_nits=[double]::Parse($parts[3],$culture)
   bits_per_color=[int]$parts[4]
   color_space=[int]$parts[5]
   red_primary=[ordered]@{x=[double]::Parse($parts[6],$culture);y=[double]::Parse($parts[7],$culture)}
   green_primary=[ordered]@{x=[double]::Parse($parts[8],$culture);y=[double]::Parse($parts[9],$culture)}
   blue_primary=[ordered]@{x=[double]::Parse($parts[10],$culture);y=[double]::Parse($parts[11],$culture)}
   white_point=[ordered]@{x=[double]::Parse($parts[12],$culture);y=[double]::Parse($parts[13],$culture)}
   source='dxgi-output-desc1'
  }
 }
}
$displayPeak=$null;$displayFullFrame=$null;$displayBlack=$null;$displayContrast=$null;$displaySource='unavailable'
$edidPeak=if($edidTarget -and $edidTarget.peak_10pct_nits -ge 40 -and $edidTarget.peak_10pct_nits -le 10000){$edidTarget.peak_10pct_nits}elseif($edidTarget -and $edidTarget.peak_full_nits -ge 40 -and $edidTarget.peak_full_nits -le 10000){$edidTarget.peak_full_nits}else{$null}
$iccIsGeneric=if($profile){[IO.Path]::GetFileName($profile) -match '^(sRGB Color Space Profile|scRGB Color Space Profile)\.icm$'}else{$false}
$dxgiPeak=if($dxgiTarget -and $dxgiTarget.max_luminance_nits -ge 40 -and $dxgiTarget.max_luminance_nits -le 10000){$dxgiTarget.max_luminance_nits}else{$null}
$dxgiFull=if($dxgiTarget -and $dxgiTarget.max_full_frame_luminance_nits -ge 40 -and $dxgiTarget.max_full_frame_luminance_nits -le 10000){$dxgiTarget.max_full_frame_luminance_nits}else{$null}
$dxgiBlack=if($dxgiTarget -and $dxgiTarget.min_luminance_nits -gt 0 -and $dxgiTarget.min_luminance_nits -lt 5){$dxgiTarget.min_luminance_nits}else{$null}
if($null -ne $dxgiPeak){
 $displayPeak=[math]::Round($dxgiPeak,6);$displayFullFrame=$(if($null -ne $dxgiFull){[math]::Round($dxgiFull,6)}else{$displayPeak});$displayBlack=$dxgiBlack;$displaySource='dxgi-output-desc1'
 if($displayBlack){$displayContrast=[math]::Round($displayPeak/$displayBlack,6)}
} elseif($iccTarget -and $null -ne $iccTarget.peak_nits -and (-not $iccIsGeneric) -and ($iccTarget.peak_nits -gt 100 -or $null -eq $edidPeak)){
 $displayPeak=$iccTarget.peak_nits;$displayBlack=$iccTarget.black_nits;$displayContrast=$iccTarget.contrast;$displaySource=$iccTarget.source
} elseif($null -ne $edidPeak) {
 $displayPeak=[math]::Round($edidPeak,6);$displayFullFrame=$(if($edidTarget.peak_full_nits -ge 40){[math]::Round($edidTarget.peak_full_nits,6)}else{$displayPeak});$displaySource=$edidTarget.source
} elseif($iccTarget -and $null -ne $iccTarget.peak_nits) {
 $displayPeak=$iccTarget.peak_nits;$displayBlack=$iccTarget.black_nits;$displayContrast=$iccTarget.contrast;$displaySource=$iccTarget.source
}
if($null -eq $displayFullFrame -and $null -ne $displayPeak){$displayFullFrame=$displayPeak}
try {
 $packages=@(Get-AppxPackage -Name 'DolbyLaboratories.DolbyVisionAccess')
 $valid=@($packages | Where-Object {
  $_.Publisher -eq 'CN=58D26209-1D57-482C-B403-B655571B5C7B' -and
  [string]$_.SignatureKind -eq 'Store' -and [string]$_.Status -eq 'Ok'
 })
 $rendererProfiles=@($valid | ForEach-Object {
  $manifest=Join-Path $_.InstallLocation 'AppxManifest.xml'
  if(Test-Path -LiteralPath $manifest -PathType Leaf){
   try {
    [xml]$xml=Get-Content -LiteralPath $manifest -Raw
    @($xml.SelectNodes("//*[local-name()='VideoRendererExtensionProfile']") | ForEach-Object {$_.Profile})
   } catch {}
  }
 } | Where-Object {$_} | Sort-Object -Unique)
 $knownProfiles=@('dvhe.05','dvhe.07','dvhe.08','dvav.09','dav1.10')
 $rendererSupport=[ordered]@{};foreach($name in $knownProfiles){$rendererSupport[$name]=@($rendererProfiles) -contains $name}
 [ordered]@{schema=6;installed=($valid.Count -gt 0);state= $(if($valid.Count){'installed-unverified'}else{'missing'});screen_count=$screenCount;output_icc=$profile;display_name=$DisplayName;monitor_id=$monitorId;display_scope=$(if($DisplayName){'mpv-window-output'}else{'primary-desktop'});display_color_mode=$displayColorMode;display_hdr_supported=$displayHdrSupported;display_hdr_user_enabled=$displayHdrUserEnabled;display_hdr_active=$displayHdrActive;display_bits_per_color=$displayBitsPerColor;display_color_encoding=$displayColorEncoding;advanced_color_api_version=$advancedColorVersion;display_peak_nits=$displayPeak;display_full_frame_nits=$displayFullFrame;display_black_nits=$displayBlack;display_contrast=$displayContrast;display_target_source=$displaySource;dxgi_output=$dxgiTarget;icc_is_generic=$iccIsGeneric;icc_profile_data=$iccTarget;displayid_target=$edidTarget;renderer_profiles=$rendererProfiles;renderer_profile_support=$rendererSupport;advanced_profile_inputs=$knownProfiles;render_verified=$false;license_entitlement_verified=$false;install_source='Microsoft Store or device manufacturer';packages=@($valid | ForEach-Object {@{name=$_.Name;version=[string]$_.Version}})} | ConvertTo-Json -Depth 8 -Compress
} catch {
 [ordered]@{schema=1;installed=$false;state='probe-failed';render_verified=$false;license_entitlement_verified=$false} | ConvertTo-Json -Compress
 exit 1
}
