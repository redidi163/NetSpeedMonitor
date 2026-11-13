@echo off
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found: %VCVARS%
  exit /b 1
)

rem vcvars64 leaves errorlevel=1 even on success, so probe cl.exe instead.
call "%VCVARS%" >nul
where cl >nul 2>&1
if errorlevel 1 (
  echo cl.exe not on PATH after vcvars
  exit /b 1
)

if not exist bin mkdir bin

cl /nologo /O2 /std:c++20 /MT /W4 /permissive- /utf-8 /EHsc /DUNICODE /D_UNICODE ^
  src\main.cpp src\adapter.cpp src\common.cpp src\settings.cpp src\ball.cpp src\tray.cpp ^
  src\etw.cpp src\aggregate.cpp src\panel.cpp ^
  /Fo:bin\ /Fe:bin\NetSpeedMonitor.exe ^
  /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" ^
  user32.lib gdi32.lib gdiplus.lib iphlpapi.lib shell32.lib advapi32.lib

if errorlevel 1 exit /b 1
echo built bin\NetSpeedMonitor.exe