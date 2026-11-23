@echo off
rem Compila zelda3.exe (Release x64) con Visual Studio Build Tools, sin MSBuild/NuGet.
rem Uso: build_msvc.bat [salida.exe] [defines extra, ej. /DREWIND_SELFTEST]
setlocal
cd /d "%~dp0"
set OUT=%~1
if "%OUT%"=="" set OUT=bin\zelda3.exe
set EXTRA=%~2
set SDL2=third_party\SDL2-2.26.5

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist bin mkdir bin
if not exist obj mkdir obj
for %%F in ("%OUT%") do set OBJDIR=obj\%%~nF
if not exist %OBJDIR% mkdir %OBJDIR%

rc /nologo /fo %OBJDIR%\zelda3.res src\platform\win32\zelda3.rc || exit /b 1

cl /nologo /O2 /MT /std:c11 /W3 /utf-8 /wd4244 /wd4018 /wd4996 /MP ^
  /DWIN32 /DNDEBUG /D_CONSOLE /D_CRT_SECURE_NO_WARNINGS %EXTRA% ^
  /I. /I%SDL2%\include /Fo%OBJDIR%\ /Fe%OUT% ^
  src\*.c src\platform\win32\volume_control.c snes\*.c ^
  third_party\gl_core\gl_core_3_1.c third_party\opus-1.3.1-stripped\opus_decoder_amalgam.c ^
  %OBJDIR%\zelda3.res ^
  /link /SUBSYSTEM:CONSOLE /LIBPATH:%SDL2%\lib\x64 SDL2.lib ^
  kernel32.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib
exit /b %ERRORLEVEL%
