param([Parameter(Mandatory=$true)][string]$Rom,
      [string]$BuildDir='build-windows/release',
      [switch]$SkipDixie,
      [switch]$SkipPublicTests)
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from an x64 Visual Studio Developer PowerShell (C++ desktop tools installed).'
}
git submodule update --init --recursive
if ($LASTEXITCODE) { throw 'Submodule initialization failed' }
python scripts/generate_snesrecomp.py --rom $Rom --analysis-backend python
if ($LASTEXITCODE) { throw 'Private ROM generation failed' }
$dixie = 'OFF'
if (!$SkipDixie) {
    # The Dixie sibling is generated from the same clean ROM (embedded patch).
    python scripts/generate_dixie_sources.py --rom $Rom
    if ($LASTEXITCODE) { throw 'Private Dixie generation failed' }
    $dixie = 'ON'
}
cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSNESRECOMP_SDL_BACKEND=SDL2 "-DDKC1_BUILD_DIXIE_VARIANT=$dixie"
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $BuildDir --parallel 6
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE) { throw 'Native Windows tests failed' }
if (!$SkipPublicTests) {
    python -m unittest discover -s tests -v
    if ($LASTEXITCODE) { throw 'Public test suite failed' }
}
Write-Host "Built $BuildDir/DKC1Recomp.exe (keep SDL2.dll and dkc1_dixie_desktop.exe alongside it)."
