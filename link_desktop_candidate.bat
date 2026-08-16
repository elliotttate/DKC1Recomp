@echo off
setlocal
rem Link the already-compiled tool-session objects under a distinct name.
rem This lets a candidate be inspected while the primary visible executable
rem is still running and therefore locked by Windows.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
cd /d "%~dp0build\hostobj_tools"
dir /b *.obj > objects.rsp
link /nologo /out:..\dkc1_desktop_candidate.exe @objects.rsp ^
  ..\main_win32.obj ws2_32.lib user32.lib gdi32.lib winmm.lib
if errorlevel 1 exit /b 1
echo CANDIDATE_LINK_OK
