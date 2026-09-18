param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$evidence = (Resolve-Path -LiteralPath $EvidenceDirectory).Path
$pm = Join-Path $evidence 'PresentMon.exe'
$request = Join-Path $evidence 'trace-request.json'
$response = Join-Path $evidence 'trace-response.json'
$stop = Join-Path $evidence 'trace-helper-stop'
$session = 'DKC1PacingHardening'
$active = $false
$collector = $null
$current = ''
$kernel = $false

function Reply($value) {
  $temporary = $response + '.tmp'
  $value | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporary
  Move-Item -LiteralPath $temporary -Destination $response -Force
}
function FinishCapture {
  if ($script:kernel) {
    & wpr.exe -stop (Join-Path $evidence ($script:current + '.etl')) 'DKC1 frame pacing' -skipPdbGen -compress -instancename $session *> (Join-Path $evidence ($script:current + '-wpr-stop.txt'))
    if ($LASTEXITCODE -ne 0) { throw 'WPR stop failed; see capture log' }
    $script:kernel = $false
  }
  if ($script:collector -and -not $script:collector.HasExited) {
    # PresentMon prints a harmless option warning on stderr even on success.
    # Windows PowerShell must not turn that warning into a terminating error.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $pm --terminate_existing_session --session_name $session *> (Join-Path $evidence ($script:current + '-pm-stop.txt'))
    $stopResult = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($stopResult -ne 0) { throw 'PresentMon session stop failed; see capture log' }
    if (-not $script:collector.WaitForExit(10000)) { throw 'PresentMon did not close' }
  }
  $script:active = $false
}

try {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Administrator approval is required for WPR and PresentMon tracing'
  }
  Reply @{status='ready'; helper_pid=$PID; scope='PresentMon and WPR only'; evidence=$evidence}
  $deadline = [DateTime]::UtcNow.AddHours(4)
  while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $stop)) {
    if (-not (Test-Path -LiteralPath $request)) { Start-Sleep -Milliseconds 100; continue }
    $command = Get-Content -LiteralPath $request -Raw | ConvertFrom-Json
    Remove-Item -LiteralPath $request
    try {
      switch ($command.action) {
        'start' {
          if ($active) { throw 'A capture is already active' }
          if ($command.name -notmatch '^[a-z0-9-]{1,64}$') { throw 'Invalid capture name' }
          $current = $command.name
          if (Test-Path -LiteralPath (Join-Path $evidence ($current + '-presentmon.csv'))) { throw 'Capture already exists' }
          if ($command.kernel) {
            $profile = (Join-Path $PSScriptRoot 'pacing_waits.wprp') + '!DKC1Waits'
            & wpr.exe -start $profile -instancename $session *> (Join-Path $evidence ($current + '-wpr-start.txt'))
            if ($LASTEXITCODE -ne 0) { throw 'WPR start failed; see capture log' }
            $kernel = $true
          }
          $arguments = @('--output_file',('"' + (Join-Path $evidence ($current + '-presentmon.csv')) + '"'),
            '--qpc_time_ms','--v1_metrics','--no_console_stats',
            '--no_track_gpu','--no_track_input','--no_track_display',
            '--session_name',$session)
          $collector = Start-Process -FilePath $pm -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $evidence ($current + '-pm-stdout.txt')) -RedirectStandardError (Join-Path $evidence ($current + '-pm-stderr.txt'))
          Start-Sleep -Milliseconds 500
          if ($collector.HasExited) { throw 'PresentMon exited during startup' }
          $active = $true
          Reply @{status='recording'; name=$current; kernel=$kernel; collector_pid=$collector.Id; arguments=$arguments}
        }
        'stop' {
          if (-not $active) { throw 'No capture is active' }
          FinishCapture
          Reply @{status='stopped'; name=$current}
        }
        default { throw 'Only start and stop capture requests are accepted' }
      }
    } catch {
      Reply @{status='error'; message=$_.Exception.Message; name=$current}
    }
  }
} catch {
  Reply @{status='error'; message=$_.Exception.Message}
} finally {
  if ($active -or $kernel) { FinishCapture }
}
