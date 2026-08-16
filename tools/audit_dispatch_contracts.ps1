[CmdletBinding()]
param(
    [string]$DisassemblyRoot = 'D:\Downloads\DKLR\DKC1_Disassembly\DKC1',
    [string]$OutputDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$asm = Join-Path $DisassemblyRoot 'Routine_Macros_DKC1.asm'
$sym = Join-Path $DisassemblyRoot 'DKC1_U1.sym'
$output = Join-Path $repo $OutputDirectory

if (-not (Test-Path -LiteralPath $asm -PathType Leaf)) {
    throw "Assembly source not found: $asm"
}
if (-not (Test-Path -LiteralPath $sym -PathType Leaf)) {
    throw "Asar symbol file not found: $sym"
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

$tableReport = Join-Path $output 'indirect-table-audit.json'
$animationReport = Join-Path $output 'animation-dispatch-audit.json'

& python (Join-Path $PSScriptRoot 'audit_indirect_tables.py') `
    --asm $asm --sym $sym --cfg-dir (Join-Path $repo 'recomp') `
    --json $tableReport
if ($LASTEXITCODE -ne 0) {
    throw 'Indirect table audit failed.'
}
& python (Join-Path $PSScriptRoot 'audit_animation_dispatch.py') `
    --asm $asm --sym $sym --cfg (Join-Path $repo 'recomp\bankbe.cfg') `
    --json $animationReport
if ($LASTEXITCODE -ne 0) {
    throw 'Animation dispatch audit failed.'
}

$tables = Get-Content -LiteralPath $tableReport -Raw | ConvertFrom-Json
$animation = Get-Content -LiteralPath $animationReport -Raw | ConvertFrom-Json
$unproven = @($tables.results | Where-Object status -eq 'unproven')
$expectedExternal = @($unproven | Where-Object requested_label -eq 'CODE_BE8179')
if ($tables.counts.failed -ne 0 -or $tables.counts.passed -ne 118 -or
    $unproven.Count -ne 1 -or $expectedExternal.Count -ne 1) {
    throw ('Unexpected indirect-dispatch proof surface: ' +
        ($tables.counts | ConvertTo-Json -Compress))
}
if (-not $animation.passed -or $animation.expected_unique_targets -ne 197) {
    throw 'Animation dispatch contract is not the exact 197-target set.'
}

[pscustomobject]@{
    IndirectContracts = $tables.contracts
    TableOrRecordProven = $tables.counts.passed
    AnimationUses = $animation.op81_call_count
    AnimationTargets = $animation.expected_unique_targets
    MissingTargets = @($animation.missing_targets).Count
    ExtraTargets = @($animation.extra_targets).Count
    Result = 'PASS'
} | Format-List
