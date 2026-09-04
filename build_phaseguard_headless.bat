@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
cd /d "%~dp0build\hostobj_tools"
set DEFS=/DSNESRECOMP_TRACE=0 /DSNESRECOMP_REVERSE_DEBUG=0 /DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 /DSYSTEM_VOLUME_MIXER_AVAILABLE=0 /D_CRT_SECURE_NO_WARNINGS
set INCS=/I..\..\snesrecomp\runner\src /I..\..\snesrecomp\runner\src\snes /I..\..\recomp /I..\..\runner
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_video.obj ..\..\runner\dkc1_video.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_baby_kong.obj ..\..\runner\dkc1_baby_kong.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_baby_kong_movement.obj ..\..\runner\dkc1_baby_kong_movement.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_margin_proxy.obj ..\..\runner\dkc1_margin_proxy.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:dkc1_game.obj ..\..\runner\dkc1_game.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:bankbf_part08_v2.obj ..\..\generated\snesrecomp\bankbf_part08_v2.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:bankbf_part00_v2.obj ..\..\generated\snesrecomp\bankbf_part00_v2.c
if errorlevel 1 exit /b 1
cl /nologo /c /W0 /O1 %DEFS% %INCS% /Fo:bankbf_part04_v2.obj ..\..\generated\snesrecomp\bankbf_part04_v2.c
if errorlevel 1 exit /b 1
dir /b *.obj > objects.rsp
link /nologo /out:..\dkc1_headless_phaseguard.exe @objects.rsp ^
  ..\main_headless.obj ws2_32.lib user32.lib
if errorlevel 1 exit /b 1
echo PHASEGUARD_HEADLESS_BUILD_OK
