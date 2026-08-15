param(
    [ValidateSet('map', 'bounds', 'gameplay', 'early', 'mid', 'late', 'route-end')]
    [string]$Anchor = 'gameplay',
    [string]$RomPath = $env:DKC1_ROM,
    [switch]$Trace
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\dkc1_desktop_tools.exe'
$snapshotNames = @{
    map = 'jungle-map-before-entry.snapshot'
    bounds = 'jungle-first-wide-bounds.snapshot'
    gameplay = 'jungle-stable-gameplay.snapshot'
    early = 'jungle-scroll-early.snapshot'
    mid = 'jungle-scroll-mid.snapshot'
    late = 'jungle-scroll-late.snapshot'
    'route-end' = 'jungle-route-end.snapshot'
}
$snapshot = Join-Path $repo ('build\snapshots\' + $snapshotNames[$Anchor])

if (-not $RomPath) {
    $candidate = 'D:\Downloads\DKLR\DKC-Recomp\DKC1_USA1.sfc'
    if (Test-Path -LiteralPath $candidate) {
        $RomPath = $candidate
    }
}
foreach ($path in @($exe, $snapshot, $RomPath)) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) {
        throw "Required file is missing: $path"
    }
}

$env:DKC1_SAVESTATE_INPUT = (Resolve-Path -LiteralPath $snapshot).Path
$env:DKC1_WIDESCREEN = '1'
$env:DKC1_FLIGHT_RECORDER = '1'
$env:DKC1_FLIGHT_RECORDER_DIR = Join-Path $repo 'build\visible-flight'
$env:DKC1_SCRIPT = $null
if ($Trace) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $env:DKC1_WS_TRACE = Join-Path $repo "build\snapshot-$Anchor-$stamp.jsonl"
} else {
    $env:DKC1_WS_TRACE = '0'
}

$process = Start-Process -FilePath $exe `
    -ArgumentList @('"' + (Resolve-Path -LiteralPath $RomPath).Path + '"') `
    -WorkingDirectory $repo -PassThru
Write-Host "Visible DKC1Recomp PID $($process.Id)"
Write-Host "Anchor: $Anchor"
Write-Host "Snapshot: $snapshot"
Write-Host "SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $snapshot).Hash)"
Write-Host 'F11 quick-save, F12 quick-load, F9 export rolling repro.'
