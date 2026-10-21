#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Host','Client')][string]$Role,
    [string]$GameRoot=$PSScriptRoot
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath (Join-Path $GameRoot 'game_dx12_ship_replay.exe')).Path
$adapters=Get-NetAdapter | Where-Object {$_.Status -eq 'Up' -and ($_.Name -like '*Radmin*' -or $_.InterfaceDescription -like '*Radmin*')}
$ips=@($adapters | Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like '26.*'} | Select-Object -ExpandProperty IPAddress)
if($ips.Count -ne 1) {throw 'Connect one Radmin adapter before adding the rules.'}
$taskHash=[Security.Cryptography.SHA256]::Create()
try {$key=([BitConverter]::ToString($taskHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($exe.ToLowerInvariant())))).Replace('-','').Substring(0,12)} finally {$taskHash.Dispose()}
$rules=@(@{Suffix='UDP';Protocol='UDP';Ports=@('3074','27127')})
if($Role -eq 'Host') {
    $rules[0].Ports+=@('3075','27126')
    $rules+=@{Suffix='TCP';Protocol='TCP';Ports=@('27128')}
}
foreach($rule in $rules) {
    $name="IW8-Radmin-$key-$Role-$($rule.Suffix)"
    # Replace only this script's exact named rule for this game/role.
    Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    New-NetFirewallRule -Name $name -DisplayName $name -Direction Inbound -Action Allow -Enabled True -Profile Any -Program $exe -Protocol $rule.Protocol -LocalPort $rule.Ports -LocalAddress $ips[0] -RemoteAddress '26.0.0.0/8' | Out-Null
    Write-Host "Allowed $($rule.Protocol) $($rule.Ports -join ',') on Radmin $($ips[0]) for $exe"
}
