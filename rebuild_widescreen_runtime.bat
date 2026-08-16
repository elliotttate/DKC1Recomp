@echo off
setlocal
rem Rebuild the host-side widescreen runtime and relink a headless candidate
rem without replacing the visible desktop executable. Generated-code changes
rem still require build_host.bat first. Recompile the two core units that own
rem the presentation-only speculative-execution boundary; all other generated
rem and core objects remain from the last complete build.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
cd /d "%~dp0build\hostobj"
set DEFS=/DSNESRECOMP_TRACE=0 /DSNESRECOMP_REVERSE_DEBUG=0 /DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 /DSYSTEM_VOLUME_MIXER_AVAILABLE=0 /D_CRT_SECURE_NO_WARNINGS
set INCS=/I..\..\snesrecomp\runner\src /I..\..\snesrecomp\runner\src\snes /I..\..\recomp /I..\..\runner
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:common_rtl.obj ..\..\snesrecomp\runner\src\common_rtl.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:interp_bridge.obj ..\..\snesrecomp\runner\src\snes\interp_bridge.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_video.obj ..\..\runner\dkc1_video.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_margin_proxy.obj ..\..\runner\dkc1_margin_proxy.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_game.obj ..\..\runner\dkc1_game.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_ws_trace.obj ..\..\runner\dkc1_ws_trace.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_debug_dump.obj ..\..\runner\dkc1_debug_dump.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_flight_recorder.obj ..\..\runner\dkc1_flight_recorder.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_blank_scan.obj ..\..\runner\dkc1_blank_scan.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_invariant_monitor.obj ..\..\runner\dkc1_invariant_monitor.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:ws_shadow.obj ..\..\snesrecomp\runner\src\snes\ws_shadow.c
if errorlevel 1 exit /b 1
dir /b *.obj > objects.rsp
link /nologo /out:..\dkc1_widescreen_headless_candidate.exe @objects.rsp ..\main_headless.obj ws2_32.lib user32.lib
if errorlevel 1 exit /b 1
link /nologo /out:..\dkc1_widescreen_desktop_next.exe @objects.rsp ..\main_win32.obj ws2_32.lib user32.lib gdi32.lib winmm.lib comdlg32.lib
if errorlevel 1 exit /b 1
echo WIDESCREEN_RUNTIME_CANDIDATE_BUILD_OK
