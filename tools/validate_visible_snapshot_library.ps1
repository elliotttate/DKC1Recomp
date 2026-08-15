param(
    [string]$RomPath = $env:DKC1_ROM,
    [int]$AutoCloseMs = 350
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\dkc1_desktop.exe'
$script = Join-Path $repo 'recipes\snapshot_smoke.dks'
$snapshotDir = Join-Path $repo 'build\snapshots'
$names = @(
    'jungle-scroll-early.snapshot',
    'jungle-scroll-mid.snapshot',
    'jungle-scroll-late.snapshot',
    'jungle-route-end.snapshot'
)

if (-not $RomPath) {
    $candidate = 'D:\Downloads\DKLR\DKC-Recomp\DKC1_USA1.sfc'
    if (Test-Path -LiteralPath $candidate) {
        $RomPath = $candidate
    }
}
foreach ($path in @($exe, $script, $RomPath)) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) {
        throw "Required file is missing: $path"
    }
}

$rows = foreach ($name in $names) {
    $snapshot = Join-Path $snapshotDir $name
    if (-not (Test-Path -LiteralPath $snapshot)) {
        throw "Snapshot is missing: $snapshot"
    }
    $tag = [IO.Path]::GetFileNameWithoutExtension($name)
    $session = Join-Path $snapshotDir ("validation-" + $tag)
    New-Item -ItemType Directory -Path $session -Force | Out-Null
    $result = Join-Path $session 'result.json'
    $checkpointIndex = Join-Path $session 'checkpoints.jsonl'
    Remove-Item -LiteralPath $result -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $checkpointIndex -Force -ErrorAction SilentlyContinue

    $env:DKC1_SAVESTATE_INPUT = (Resolve-Path -LiteralPath $snapshot).Path
    $env:DKC1_SCRIPT = (Resolve-Path -LiteralPath $script).Path
    $env:DKC1_SESSION_DIR = $session
    $env:DKC1_WIDESCREEN = '1'
    $env:DKC1_WS_TRACE = '0'
    $env:DKC1_FLIGHT_RECORDER = '0'
    $env:DKC1_ROUTE_RESULT = $result
    $env:DKC1_ROUTE_AUTOCLOSE_MS = [string]$AutoCloseMs
    $env:DKC1_ROUTE_FRAME_LIMIT = '10'

    $process = Start-Process -FilePath $exe `
        -ArgumentList @('"' + (Resolve-Path -LiteralPath $RomPath).Path + '"') `
        -WorkingDirectory $repo -PassThru
    $process.WaitForExit()
    if (-not (Test-Path -LiteralPath $result)) {
        throw "Visible validation exited without a result: $name"
    }
    $routeResult = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
    if ($routeResult.status -ne 'complete') {
        throw "Visible validation failed for $name`: $($routeResult.status)"
    }
    if (-not (Test-Path -LiteralPath $checkpointIndex)) {
        throw "Visible validation did not capture checkpoint evidence: $name"
    }
    $checkpoint = Get-Content -LiteralPath $checkpointIndex -Raw |
        ConvertFrom-Json
    [ordered]@{
        snapshot = $name
        snapshotSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $snapshot).Hash
        status = $routeResult.status
        snesFrame = $routeResult.snes_frame
        wram = $checkpoint.wram
        vram = $checkpoint.vram
        oamShadow = $checkpoint.oam_shadow
    }
}

$validation = [ordered]@{
    schema = 'dkc1.snapshot-library-validation.v1'
    validatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    visible = $true
    snapshots = @($rows)
}
$output = Join-Path $snapshotDir 'jungle-route-validation.json'
$validation | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $output -Encoding utf8
$rows | ForEach-Object {
    Write-Host "$($_.snapshot)  PASS  frame=$($_.snesFrame)  WRAM=$($_.wram)"
}
Write-Host "Validation: $output"
