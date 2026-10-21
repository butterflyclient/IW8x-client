[CmdletBinding()]
param([string]$BackendAddress='26.173.90.18', [int]$Port=27128)
$ErrorActionPreference='Stop'
# The minimal service accepts complete Content-Length requests, not 100-continue.
[Net.ServicePointManager]::Expect100Continue=$false
$origin="http://${BackendAddress}:$Port"
try {
    $playlist=Invoke-RestMethod -Method Post -Uri "$origin/v1/playlists" -ContentType 'application/json' -Body '{"protocol":1,"build":"iw8-1.20.4"}' -TimeoutSec 8
    $publisher=Invoke-RestMethod -Method Post -Uri "$origin/v1/publisher-variables" -ContentType 'application/json' -Body '{"version":1,"context":"","op":"versions"}' -TimeoutSec 8
    Write-Host "Backend reached at $origin"
    Write-Host "Playlist SHA256: $($playlist.sha256)"
    Write-Host "Publisher namespaces: $($publisher.namespaces.PSObject.Properties.Name -join ', ')"
    Write-Host 'HTTP is reachable. Gameplay UDP, party joins and chat still need an in-game test.'
} catch {
    Write-Error "Cannot read backend at $origin. Start the host game/dedicated server, join the same Radmin network, and check the host firewall. $($_.Exception.Message)"
}
