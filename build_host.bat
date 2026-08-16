@echo off
rem Build the DKC1 headless host with MSVC directly (no CMake required).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
cd /d %~dp0
rem Build identity: git commit (+dirty), timestamp, config -> window title,
rem debug panel, and save-state sidecars (stale-executable detection).
set DKC1_GIT=nogit
for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set DKC1_GIT=%%i
git diff-index --quiet HEAD -- 2>nul || set DKC1_GIT=%DKC1_GIT%-dirty
set BUILD_ID_DEFS=/DDKC1_BUILD_COMMIT=\"%DKC1_GIT%\" "/DDKC1_BUILD_TIME=\"%DATE% %TIME:~0,5%\"" /DDKC1_BUILD_CONFIG=\"primary\"
set SR=..\..\snesrecomp\runner\src
if not exist build\hostobj mkdir build\hostobj
cd build\hostobj
set DEFS=/DSNESRECOMP_TRACE=0 /DSNESRECOMP_REVERSE_DEBUG=0 /DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 /DSYSTEM_VOLUME_MIXER_AVAILABLE=0 /D_CRT_SECURE_NO_WARNINGS
set INCS=/I%SR% /I%SR%\snes /I..\..\recomp /I..\..\runner

cl /nologo /c /MP8 /W0 /O1 %DEFS% %INCS% ^
  %SR%\common_cpu_infra.c %SR%\common_rtl.c %SR%\widescreen.c ^
  %SR%\recomp_hw.c %SR%\framedump.c %SR%\host_paths.c ^
  %SR%\launcher.c %SR%\launcher_cache.c %SR%\launcher_picker.c ^
  %SR%\rom_image_verify.c %SR%\crc32.c %SR%\sha256.c ^
  %SR%\cpu_state.c %SR%\cpu_trace.c %SR%\audio_trace.c ^
  %SR%\ppu_dma_trace.c %SR%\execution_mode.c %SR%\util.c ^
  %SR%\snes\apu.c %SR%\snes\cart.c %SR%\snes\cpu.c %SR%\snes\dma.c ^
  %SR%\snes\dsp.c %SR%\snes\dsp1.c %SR%\snes\dsp1_hle.c ^
  %SR%\snes\joypad.c %SR%\snes\audio_shadow.c %SR%\snes\dsp_shadow.c ^
  %SR%\snes\msu1.c %SR%\snes\color_lut.c %SR%\snes\ppu.c ^
  %SR%\snes\ppu_legacy.c %SR%\snes\sa1.c %SR%\snes\ws_shadow.c ^
  %SR%\snes\snes.c %SR%\snes\snes_other.c %SR%\snes\spc.c ^
  %SR%\snes\superfx.c %SR%\snes\interp816.c %SR%\snes\tier2_capture.c ^
  %SR%\snes\interp_bridge.c %SR%\snes\cx4.c ^
  ..\..\runner\dkc1_game.c ..\..\runner\dkc1_video.c ^
  ..\..\runner\dkc1_margin_proxy.c ^
  ..\..\runner\dkc1_ws_trace.c ^
  ..\..\runner\headless_host.c ..\..\runner\input_playback.c ^
  ..\..\runner\wram_dump.c ^
  ..\..\runner\dkc1_script.c ..\..\runner\dkc1_debug_dump.c ^
  ..\..\runner\dkc1_flight_recorder.c ^
  ..\..\runner\dkc1_blank_scan.c ^
  ..\..\runner\dkc1_invariant_monitor.c ^
  ..\..\runner\verified_rom.c ^
  ..\..\generated\snesrecomp\*.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_headless.obj ..\..\runner\headless_main.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_win32.obj ..\..\runner\win32_host.c
if errorlevel 1 exit /b 1
set LINK_RETRIES=0
dir /b *.obj > objects.rsp
:link_headless
link /nologo /out:..\dkc1_snesrecomp_headless.exe @objects.rsp ..\main_headless.obj ws2_32.lib user32.lib
if not errorlevel 1 goto link_desktop_begin
set /a LINK_RETRIES+=1
if %LINK_RETRIES% GEQ 5 exit /b 1
timeout /t 2 /nobreak >nul
goto link_headless

:link_desktop_begin
set LINK_RETRIES=0
:link_desktop
link /nologo /out:..\dkc1_desktop.exe @objects.rsp ..\main_win32.obj ws2_32.lib user32.lib gdi32.lib winmm.lib comdlg32.lib
if not errorlevel 1 goto build_ok
set /a LINK_RETRIES+=1
if %LINK_RETRIES% GEQ 5 exit /b 1
timeout /t 2 /nobreak >nul
goto link_desktop

:build_ok
echo HOST_BUILD_OK
