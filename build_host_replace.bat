@echo off
rem DKC1_REPLACE A/B build: the trace host with staged routine
rem replacements linked in (see docs/MOD_LAYER.md). Fails closed unless
rem tools/gen_replacements.py verified every replacement against the
rem supported ROM first:
rem   python tools\gen_replacements.py --rom <rom> [--bless]
rem Compare against the stock build\dkc1_headless_trace.exe with
rem tools/oracle_run.py + oracle_diff.py; DKC1_REPLACE_DISABLE=1 runs
rem the untouched originals at runtime.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
cd /d %~dp0
if not exist build\replacements\overrides.bat (
  echo missing build\replacements\overrides.bat - run tools\gen_replacements.py first
  exit /b 1
)
set DKC1_GIT=nogit
for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set DKC1_GIT=%%i
git diff-index --quiet HEAD -- 2>nul || set DKC1_GIT=%DKC1_GIT%-dirty
set BUILD_ID_DEFS=/DDKC1_BUILD_COMMIT=\"%DKC1_GIT%\" "/DDKC1_BUILD_TIME=\"%DATE% %TIME:~0,5%\"" /DDKC1_BUILD_CONFIG=\"replace\"
set SR=..\..\snesrecomp\runner\src
if not exist build\hostobj_replace mkdir build\hostobj_replace
cd build\hostobj_replace
set DEFS=/DSNESRECOMP_TRACE=0 /DSNESRECOMP_FUNC_ENTRY_HOOK=1 /DSNESRECOMP_REVERSE_DEBUG=0 /DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 /DSYSTEM_VOLUME_MIXER_AVAILABLE=0 /D_CRT_SECURE_NO_WARNINGS
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
  ..\..\runner\dkc1_baby_kong.c ..\..\runner\dkc1_baby_kong_movement.c ^
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
rem Replacement takeover: recompile defining TUs with the variant symbol
rem renamed to *_original (overwrites their .obj), then compile the
rem replacement sources that define the takeover symbols.
call ..\replacements\overrides.bat
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% %BUILD_ID_DEFS% /Fo:..\main_headless_replace.obj ..\..\runner\headless_main.c
if errorlevel 1 exit /b 1
dir /b *.obj > objects.rsp
link /nologo /out:..\dkc1_headless_replace_trace.exe @objects.rsp ..\main_headless_replace.obj ws2_32.lib user32.lib
if errorlevel 1 exit /b 1
echo REPLACE_BUILD_OK
