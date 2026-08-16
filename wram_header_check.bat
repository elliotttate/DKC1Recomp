@echo off
rem Compile gate for the generated WRAM header: it must build standalone
rem under the same MSVC the hosts use. Run from repo root:
rem   wram_header_check.bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
cd /d %~dp0..
echo #include "runner/dkc1_wram_gen.h" > build\wram_check_tu.c
echo int main(void) { unsigned char w[0x20000] = {0}; >> build\wram_check_tu.c
echo   Dkc1WramSetU16(w, DKC1_WRAM_SprXPos + 2, 0x1234); >> build\wram_check_tu.c
echo   return Dkc1Actor_SprXPos(w, 2) == 0x1234 ? 0 : 1; } >> build\wram_check_tu.c
cl /nologo /W4 /WX /I. /Fe:build\wram_check_tu.exe build\wram_check_tu.c /Fo:build\wram_check_tu.obj
if errorlevel 1 exit /b 1
build\wram_check_tu.exe
if errorlevel 1 (echo RUNTIME CHECK FAILED & exit /b 1)
echo wram header compile+runtime check OK
