@echo off
setlocal
rem Incrementally rebuild runner-only diagnostics after a successful full
rem tool-session build. Core/PPU/generated objects remain the exact objects
rem from that build; outputs use candidate names and never replace a running
rem visible executable.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
cd /d "%~dp0"
set DKC1_GIT=nogit
for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set DKC1_GIT=%%i
git diff-index --quiet HEAD -- 2>nul || set DKC1_GIT=%DKC1_GIT%-dirty
set BUILD_ID_DEFS=/DDKC1_BUILD_COMMIT=\"%DKC1_GIT%\" "/DDKC1_BUILD_TIME=\"%DATE% %TIME:~0,5%\"" /DDKC1_BUILD_CONFIG=\"tools-candidate\"
set SR=..\..\snesrecomp\runner\src
set DEFS=/DSNESRECOMP_TRACE=0 /DSNESRECOMP_REVERSE_DEBUG=0 /DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 /DSYSTEM_VOLUME_MIXER_AVAILABLE=0 /D_CRT_SECURE_NO_WARNINGS
set INCS=/I%SR% /I%SR%\snes /I..\..\recomp /I..\..\runner
cd build\hostobj_tools
rem Rebuild the shared presentation/runtime objects too. Candidate builds are
rem used specifically while the visible tools executable is locked; retaining
rem these objects from the last full build would silently omit the fix under
rem test even though the candidate link succeeds.
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_game.obj ..\..\runner\dkc1_game.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_baby_kong.obj ..\..\runner\dkc1_baby_kong.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_baby_kong_movement.obj ..\..\runner\dkc1_baby_kong_movement.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_video.obj ..\..\runner\dkc1_video.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_ws_trace.obj ..\..\runner\dkc1_ws_trace.c
if errorlevel 1 exit /b 1
rem Rebuild the generated unit that owns the private vertical-rope OAM
rem override; otherwise an incremental diagnostics link can silently retain
rem the pre-fix object from the last full build.
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:bank80_part27e_v2.obj ..\..\generated\snesrecomp\bank80_part27e_v2.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_debug_dump.obj ..\..\runner\dkc1_debug_dump.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_flight_recorder.obj ..\..\runner\dkc1_flight_recorder.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_headless.obj ..\..\runner\headless_main.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_win32.obj ..\..\runner\win32_host.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_layer_capture.obj ..\..\runner\layer_capture_main.c
if errorlevel 1 exit /b 1
dir /b *.obj > objects.rsp
link /nologo /out:..\dkc1_headless_candidate.exe @objects.rsp ^
  ..\main_headless.obj ws2_32.lib user32.lib
if errorlevel 1 exit /b 1
link /nologo /out:..\dkc1_desktop_candidate.exe @objects.rsp ^
  ..\main_win32.obj ws2_32.lib user32.lib gdi32.lib winmm.lib
if errorlevel 1 exit /b 1
link /nologo /out:..\dkc1_layer_capture_candidate.exe @objects.rsp ^
  ..\main_layer_capture.obj ws2_32.lib user32.lib
if errorlevel 1 exit /b 1
echo DIAGNOSTICS_CANDIDATE_BUILD_OK
