# SPDX-License-Identifier: MIT
# Read-only. Package presence is not proof of license entitlement or rendering.
$ErrorActionPreference='Stop'
try {
 $packages=@(Get-AppxPackage -Name 'DolbyLaboratories.DolbyVisionAccess')
 $valid=@($packages | Where-Object {
  $_.Publisher -eq 'CN=58D26209-1D57-482C-B403-B655571B5C7B' -and
  [string]$_.SignatureKind -eq 'Store' -and [string]$_.Status -eq 'Ok'
 })
 [ordered]@{schema=1;installed=($valid.Count -gt 0);state= $(if($valid.Count){'installed-unverified'}else{'missing'});render_verified=$false;license_entitlement_verified=$false;install_source='Microsoft Store or device manufacturer';packages=@($valid | ForEach-Object {@{name=$_.Name;version=[string]$_.Version}})} | ConvertTo-Json -Depth 4 -Compress
} catch {
 [ordered]@{schema=1;installed=$false;state='probe-failed';render_verified=$false;license_entitlement_verified=$false} | ConvertTo-Json -Compress
 exit 1
}
