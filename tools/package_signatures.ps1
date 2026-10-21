[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$repo=Split-Path $project -Parent
$destination=Join-Path $repo 'artifacts/porting-tools'
$stage=Join-Path $destination 'files'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$inventory=[ordered]@{
    'signature_scan.exe'=(Join-Path $project 'xmake-out/tools/signature_scan.exe')
    'signature_scanner.h'=(Join-Path $project 'src/engine/signature_scanner.h')
    'replay-tu19.json'=(Join-Path $project 'data/signatures/replay-tu19.json')
    'replay-tu19-inputs.json'=(Join-Path $project 'data/signatures/replay-tu19-inputs.json')
    'collect_bindings.py'=(Join-Path $project 'tools/porting/collect_bindings.py')
    'export_ida_signatures.py'=(Join-Path $project 'tools/porting/export_ida_signatures.py')
    'extract_embedded_playlist.py'=(Join-Path $repo 'ida-reports/extract_embedded_playlist.py')
    'README.md'=(Join-Path $repo 'SIGNATURE_PORTING.md')
    'json.LICENSE.txt'=(Join-Path $project 'thirdparty/json.LICENSE.txt')
    'replay-tu19-selfcheck.json'=(Join-Path $repo 'artifacts/signatures/replay-tu19-selfcheck.json')
    'mw144-candidates.json'=(Join-Path $repo 'artifacts/signatures/mw144-candidates.json')
    'mw144-candidates.h'=(Join-Path $repo 'artifacts/signatures/mw144-candidates.h')
    'validation.json'=(Join-Path $repo 'artifacts/signatures/validation.json')
}
$files=@()
foreach($name in $inventory.Keys){
    $target=Join-Path $stage $name
    Copy-Item -LiteralPath $inventory[$name] -Destination $target -Force
    $files+=$target
}
$checksums=foreach($file in $files){'{0}  {1}' -f (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash,(Split-Path $file -Leaf)}
$hashPath=Join-Path $stage 'SHA256SUMS.txt'
$checksums | Set-Content -LiteralPath $hashPath -Encoding ascii
$files+=$hashPath
$zip=Join-Path $destination 'iw8-signature-tools.zip'
Compress-Archive -LiteralPath $files -DestinationPath $zip -Force
Write-Output "Packaged $($files.Count) files: $zip"
