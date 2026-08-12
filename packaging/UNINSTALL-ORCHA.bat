@echo off
setlocal enabledelayedexpansion
title ORCHA - uninstall

rem ---------------------------------------------------------------------------
rem  Removes every plausible ORCHA install, in BOTH shapes (folder bundle and
rem  single file), from every location an installer might have used - and
rem  verifies afterwards. This is the recovery path when an install is stuck.
rem ---------------------------------------------------------------------------

echo.
echo   ORCHA - uninstall
echo   -----------------
echo.

set "P1=%CommonProgramFiles%\VST3\ORCHA.vst3"
set "P2=%CommonProgramFiles(x86)%\VST3\ORCHA.vst3"
set "P3=%ProgramFiles%\Common Files\VST3\ORCHA.vst3"
set "P4=%LOCALAPPDATA%\Programs\Common\VST3\ORCHA.vst3"
set "P5=%ProgramFiles%\Naaman\ORCHA"

rem  A DAW holding the plug-in makes every delete below fail silently.
set "DAWRUNNING="
for %%P in ("Cubase.exe" "Nuendo.exe" "Ableton Live.exe" "reaper.exe" "FL64.exe" "Studio One.exe" "Bitwig Studio.exe") do (
  tasklist /FI "IMAGENAME eq %%~P" 2>nul | find /I "%%~P" >nul && set "DAWRUNNING=%%~P"
)
if defined DAWRUNNING (
  echo   ERROR: %DAWRUNNING% is running. Close it first - Windows will not let
  echo          this script delete a plug-in a DAW has open.
  goto :fail
)

set FOUND=0

for %%T in ("%P1%" "%P2%" "%P3%" "%P4%" "%P5%") do (
  if exist "%%~T\" (
    set /a FOUND+=1
    echo   removing folder  %%~T
    rmdir /S /Q "%%~T"
  ) else (
    if exist "%%~T" (
      set /a FOUND+=1
      echo   removing file    %%~T
      del /F /Q "%%~T"
    )
  )
)

rem  ORCHA also keeps a WAV cache of rendered loops in the temp folder. Not a
rem  plug-in location, but a clean uninstall should not leave it behind.
if exist "%TEMP%\ORCHA\" (
  set /a FOUND+=1
  echo   removing cache   %TEMP%\ORCHA
  rmdir /S /Q "%TEMP%\ORCHA"
)

if "%FOUND%"=="0" (
  echo   Nothing was installed.
  goto :done
)

rem  Verify. An uninstaller that reports success while leaving a plug-in behind
rem  is worse than one that fails.
set LEFT=0
for %%T in ("%P1%" "%P2%" "%P3%" "%P4%" "%P5%") do (
  if exist "%%~T" (
    set /a LEFT+=1
    echo   STILL PRESENT: %%~T
  )
)

if not "%LEFT%"=="0" (
  echo.
  echo   %LEFT% item(s) could not be removed. Right-click this script and
  echo   choose "Run as administrator", then try again.
  goto :fail
)

echo.
echo   Removed. Nothing left behind.
echo   In Cubase: Studio menu, then VST Plug-in Manager, then rescan.

:done
echo.
pause
exit /b 0

:fail
echo.
echo   STOPPED.
pause
exit /b 1
