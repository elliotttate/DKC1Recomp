@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
cd /d "%~dp0\.."
if not exist build\blank_scan_verifier mkdir build\blank_scan_verifier
cl /nologo /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /Irunner ^
  tests\blank_scan_detector_model.c runner\dkc1_blank_scan.c ^
  /Fo:build\blank_scan_verifier\ ^
  /Fe:build\blank_scan_verifier\blank_scan_detector.exe
if errorlevel 1 exit /b 1
build\blank_scan_verifier\blank_scan_detector.exe
exit /b %errorlevel%
