@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
cd /d C:\Users\ellio\Documents\GitHub\DKC1Recomp
if not exist build\obj mkdir build\obj
cd build\obj
cl /nologo /c /MP /W0 /O1 /I..\..\recomp /I..\..\snesrecomp\runner\src /I..\..\snesrecomp\runner\src\snes ..\..\generated\snesrecomp\*.c
if errorlevel 1 exit /b 1
lib /nologo /OUT:..\snesrecomp_game.lib *.obj
echo LIB_BUILD_OK
