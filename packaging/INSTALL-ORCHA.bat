@echo off
setlocal enabledelayedexpansion
title ORCHA - Installer

REM ---------------------------------------------------------------------------
REM  The SIMPLE installer: a prebuilt ORCHA.vst3 sits next to this file and is
REM  copied into place. No Visual Studio, no CMake, no compiling - this is the
REM  one that gets sent to other people.
REM
REM  Modeled line-for-line on TRIX's installer, the one that worked on a real
REM  machine. Its rules apply here too: no text echoed by this script may
REM  contain greater-than, less-than, ampersand, pipe, caret or parentheses.
REM
REM  INSTALL-ORCHA-BUILD.bat is the OTHER installer - it builds from source and
REM  is for developers. This one is for musicians.
REM ---------------------------------------------------------------------------

echo.
echo  ============================================
echo    ORCHA by Gussa Naaman
echo    Rhythm Loop Generator
echo  ============================================
echo.

REM --- administrator ----------------------------------------------------------
net session >nul 2>&1
if errorlevel 1 (
    echo  [X] This installer needs administrator rights.
    echo.
    echo      Close this window, RIGHT-CLICK INSTALL-ORCHA.bat
    echo      and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)
echo  [OK] Running as administrator.

REM --- the payload must be sitting next to this file ---------------------------
REM Running the .bat straight out of the ZIP viewer copies it to a temp folder
REM on its own, and then there is nothing to install. Catch that here rather
REM than failing later with an unhelpful error.
set "SRC=%~dp0ORCHA.vst3"

if not exist "%SRC%\" (
    echo  [X] ORCHA.vst3 was not found next to this installer.
    echo.
    echo      Looked in: %~dp0
    echo.
    echo      You are probably running this from inside the ZIP.
    echo      EXTRACT the whole ZIP to a real folder first,
    echo      right-click the ZIP and choose "Extract All",
    echo      then run INSTALL-ORCHA.bat from the extracted folder.
    echo.
    pause
    exit /b 1
)
echo  [OK] Found ORCHA.vst3 to install.

set "DEST=C:\Program Files\Common Files\VST3"
if not exist "%DEST%\" mkdir "%DEST%" 2>nul

REM --- warn about a running DAW ------------------------------------------------
REM A host that has ORCHA loaded holds the DLL open, and Windows will not let
REM anyone replace it. This is the most common reason an update silently does
REM nothing, so it is worth stopping for.
set "DAW="
for %%P in (Cubase.exe Nuendo.exe Ableton.exe FL64.exe FL.exe Studio One.exe reaper.exe Bitwig Studio.exe) do (
    tasklist /fi "imagename eq %%P" 2>nul | find /i "%%P" >nul && set "DAW=%%P"
)
if defined DAW (
    echo.
    echo  [!] %DAW% appears to be running.
    echo      Windows will not let the plugin be replaced while a host has it
    echo      loaded. Please close it now, then press any key to continue.
    echo.
    pause
)

REM --- remove any previous install --------------------------------------------
REM The old version may be a FOLDER bundle or a single .vst3 FILE, depending on
REM which build installed it. rmdir cannot delete a file and del cannot delete a
REM folder, so both are tried and the result is checked rather than assumed.
echo.
echo  [1/3] Removing any previous version...

if exist "%DEST%\ORCHA.vst3\" (
    echo        found a folder bundle - deleting
    rmdir /s /q "%DEST%\ORCHA.vst3"
) else if exist "%DEST%\ORCHA.vst3" (
    echo        found a single file - deleting
    del /f /q "%DEST%\ORCHA.vst3"
) else (
    echo        nothing to remove
)

if exist "%DEST%\ORCHA.vst3" (
    echo.
    echo  [X] The old ORCHA could not be removed.
    echo      Something still has it open - usually a DAW, or the plugin
    echo      scanner that runs in the background after a DAW closes.
    echo.
    echo      Close every audio application, wait a few seconds and run
    echo      this installer again.
    echo.
    pause
    exit /b 1
)
echo        done.

REM --- install ------------------------------------------------------------------
echo  [2/3] Installing ORCHA...
xcopy /e /i /y "%SRC%" "%DEST%\ORCHA.vst3\" >nul
if errorlevel 1 (
    echo.
    echo  [X] Copy failed.
    echo      Source: %SRC%
    echo      Target: %DEST%\ORCHA.vst3
    echo.
    pause
    exit /b 1
)

REM The standalone is optional: it ships only when the packager found one.
if exist "%~dp0ORCHA.exe" (
    if not exist "C:\Program Files\Naaman\ORCHA\" mkdir "C:\Program Files\Naaman\ORCHA" 2>nul
    copy /y "%~dp0ORCHA.exe" "C:\Program Files\Naaman\ORCHA\ORCHA.exe" >nul
    if not errorlevel 1 echo        standalone app installed too.
)

REM --- verify -------------------------------------------------------------------
REM Reporting success without checking is how an installer lies. The binary
REM itself has to be on disk before this says "Done".
echo  [3/3] Verifying...
if not exist "%DEST%\ORCHA.vst3\Contents\x86_64-win\ORCHA.vst3" (
    echo.
    echo  [X] The plugin binary is not where it should be after copying.
    echo      Expected: %DEST%\ORCHA.vst3\Contents\x86_64-win\ORCHA.vst3
    echo.
    pause
    exit /b 1
)
echo        verified.

echo.
echo  ============================================
echo    Done. ORCHA is installed.
echo.
echo    Installed to:
echo    %DEST%\ORCHA.vst3
echo.
echo    1. Start your DAW and rescan plugins
echo    2. ORCHA is an INSTRUMENT, not an effect.
echo       Add it on an INSTRUMENT TRACK.
echo       Look under Naaman.
echo    3. Drop up to 3 samples on it, pick a style,
echo       press GENERATE LOOPS, then drag any card
echo       straight into an audio track.
echo  ============================================
echo.
pause
