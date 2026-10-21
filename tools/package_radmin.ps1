[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$repo=Split-Path $project -Parent
$destination=Join-Path $repo 'artifacts/radmin-release'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach($role in @('host','client')) {
    $folder=Join-Path $destination $role
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $project "xmake-out/x64/Release/radmin-$role/XInput9_1_0.dll") -Destination $folder -Force
    foreach($script in @('check-backend.ps1','allow-radmin.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "radmin/$script") -Destination $folder -Force
    }
    Copy-Item -LiteralPath (Join-Path $repo 'RADMIN_CLIENTS.md') -Destination (Join-Path $folder 'README.md') -Force
    foreach($doc in @('MOTD_BACKEND.md','STORE_BACKEND.md','CUSTOM_MODS_BACKEND.md','NATIVE_UI_AUDIT.md','XP_TOKENS_BACKEND.md','USER_LISTS_BACKEND.md','PROGRESSION_REMAINING.txt','BACKEND_EMULATION_STATUS.txt')) {
        Copy-Item -LiteralPath (Join-Path $repo $doc) -Destination $folder -Force
    }
    $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $folder 'XInput9_1_0.dll')).Hash
    "$hash  XInput9_1_0.dll" | Set-Content -LiteralPath (Join-Path $folder 'SHA256.txt') -Encoding ascii
    # Explicit allowlist: never package profiles, identity keys or backend databases.
    $files=@('XInput9_1_0.dll','README.md','SHA256.txt','check-backend.ps1','allow-radmin.ps1','MOTD_BACKEND.md','STORE_BACKEND.md','CUSTOM_MODS_BACKEND.md','NATIVE_UI_AUDIT.md','XP_TOKENS_BACKEND.md','USER_LISTS_BACKEND.md','PROGRESSION_REMAINING.txt','BACKEND_EMULATION_STATUS.txt') | ForEach-Object {Join-Path $folder $_}
    Compress-Archive -LiteralPath $files -DestinationPath (Join-Path $destination "iw8-radmin-$role.zip") -Force
    Write-Host "$role SHA256 $hash"
}
