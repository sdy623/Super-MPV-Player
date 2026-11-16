# SPDX-License-Identifier: MIT
# Read-only. Package presence is not proof of license entitlement or rendering.
$ErrorActionPreference='Stop'
$screenCount=0
try { Add-Type -AssemblyName System.Windows.Forms; $screenCount=@([System.Windows.Forms.Screen]::AllScreens).Count } catch {}
$profile=''
try {
 Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class MpcDisplayProfile {
 [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr w);
 [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr w, IntPtr dc);
 [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] static extern bool GetICMProfile(IntPtr dc, ref uint n, StringBuilder name);
 public static string Read() {var dc=GetDC(IntPtr.Zero);if(dc==IntPtr.Zero)return "";try {uint n=32768;var b=new StringBuilder((int)n);return GetICMProfile(dc,ref n,b)?b.ToString():"";} finally {ReleaseDC(IntPtr.Zero,dc);}}
}
"@
 $profile=[MpcDisplayProfile]::Read()
 if(-not (Test-Path -LiteralPath $profile -PathType Leaf)){$profile=''}
} catch { $profile='' }
try {
 $packages=@(Get-AppxPackage -Name 'DolbyLaboratories.DolbyVisionAccess')
 $valid=@($packages | Where-Object {
  $_.Publisher -eq 'CN=58D26209-1D57-482C-B403-B655571B5C7B' -and
  [string]$_.SignatureKind -eq 'Store' -and [string]$_.Status -eq 'Ok'
 })
 [ordered]@{schema=1;installed=($valid.Count -gt 0);state= $(if($valid.Count){'installed-unverified'}else{'missing'});screen_count=$screenCount;output_icc=$profile;display_scope='primary-desktop';render_verified=$false;license_entitlement_verified=$false;install_source='Microsoft Store or device manufacturer';packages=@($valid | ForEach-Object {@{name=$_.Name;version=[string]$_.Version}})} | ConvertTo-Json -Depth 4 -Compress
} catch {
 [ordered]@{schema=1;installed=$false;state='probe-failed';render_verified=$false;license_entitlement_verified=$false} | ConvertTo-Json -Compress
 exit 1
}
