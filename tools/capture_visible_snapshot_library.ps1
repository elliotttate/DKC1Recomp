param(
    [string]$RomPath = $env:DKC1_ROM,
    [int]$AutoCloseMs = 1500
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\dkc1_desktop.exe'
$script = Join-Path $repo 'recipes\capture_jungle_route_snapshots.dks'
$rootSnapshot = Join-Path $repo 'build\snapshots\jungle-stable-gameplay.snapshot'
$snapshotDir = Join-Path $repo 'build\snapshots'
$result = Join-Path $snapshotDir 'capture-route-result.json'

if (-not $RomPath) {
    $candidate = 'D:\Downloads\DKLR\DKC-Recomp\DKC1_USA1.sfc'
    if (Test-Path -LiteralPath $candidate) {
        $RomPath = $candidate
    }
}
foreach ($path in @($exe, $script, $rootSnapshot, $RomPath)) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) {
        throw "Required file is missing: $path"
    }
}
if ($AutoCloseMs -lt 0 -or $AutoCloseMs -gt 60000) {
    throw 'AutoCloseMs must be between 0 and 60000.'
}

New-Item -ItemType Directory -Path $snapshotDir -Force | Out-Null
Remove-Item -LiteralPath $result -Force -ErrorAction SilentlyContinue

$env:DKC1_SAVESTATE_INPUT = (Resolve-Path -LiteralPath $rootSnapshot).Path
$env:DKC1_SCRIPT = (Resolve-Path -LiteralPath $script).Path
$env:DKC1_WIDESCREEN = '1'
$env:DKC1_WS_TRACE = '0'
$env:DKC1_FLIGHT_RECORDER = '0'
$env:DKC1_ROUTE_RESULT = $result
$env:DKC1_ROUTE_AUTOCLOSE_MS = [string]$AutoCloseMs
$env:DKC1_ROUTE_FRAME_LIMIT = '2000'

$process = Start-Process -FilePath $exe `
    -ArgumentList @('"' + (Resolve-Path -LiteralPath $RomPath).Path + '"') `
    -WorkingDirectory $repo -PassThru
Write-Host "Visible snapshot capture PID $($process.Id)"
Write-Host 'The existing interactive DKC1Recomp window was not touched.'
$process.WaitForExit()

if (-not (Test-Path -LiteralPath $result)) {
    throw "Visible capture exited without a route result: $result"
}
$routeResult = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
if ($routeResult.status -ne 'complete') {
    throw "Snapshot capture failed: $($routeResult.status) $($routeResult.message)"
}

$names = @(
    'jungle-scroll-early.snapshot',
    'jungle-scroll-mid.snapshot',
    'jungle-scroll-late.snapshot',
    'jungle-route-end.snapshot'
)
$manifestEntries = foreach ($name in $names) {
    $path = Join-Path $snapshotDir $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected snapshot was not created: $path"
    }
    [ordered]@{
        name = $name
        bytes = (Get-Item -LiteralPath $path).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    }
}
$manifest = [ordered]@{
    schema = 'dkc1.snapshot-library.v1'
    createdUtc = (Get-Date).ToUniversalTime().ToString('o')
    root = [ordered]@{
        name = (Split-Path -Leaf $rootSnapshot)
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $rootSnapshot).Hash
    }
    routeResult = $routeResult
    snapshots = @($manifestEntries)
}
$manifestPath = Join-Path $snapshotDir 'jungle-route-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "Created $($manifestEntries.Count) reusable snapshots:"
$manifestEntries | ForEach-Object {
    Write-Host "  $($_.name)  $($_.sha256)"
}
Write-Host "Manifest: $manifestPath"
