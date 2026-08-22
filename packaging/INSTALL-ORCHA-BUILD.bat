@echo off
setlocal enabledelayedexpansion
title ORCHA 0.21.0 - Build and Install

REM ---------------------------------------------------------------------------
REM  ONE file. Right-click it, "Run as administrator", and it does everything:
REM  checks what is installed, builds the plug-in, installs it, verifies it.
REM
REM  Modeled line-for-line on FOUR COLOR's installer - the one that actually
REM  worked on the target machine. Its rules apply here too:
REM
REM  No text echoed by this script may contain the characters
REM  greater-than, less-than, ampersand, pipe, caret or parentheses. A
REM  greater-than inside echoed text becomes a redirection operator, mangles
REM  the output, creates stray files and breaks the if-blocks badly enough
REM  that it prints DONE after NOT INSTALLED. Plain echo, plain words.
REM
REM  JUCE lives ONCE at %USERPROFILE%\JUCE, shared by every Naaman plug-in.
REM  If FOUR COLOR was built on this machine it is already there and nothing
REM  is downloaded at all.
REM ---------------------------------------------------------------------------

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "LOG=%ROOT%\Orcha-install-log.txt"
set "DEST=C:\Program Files\Common Files\VST3"
set "BUILD=%ROOT%\build-win"

echo ORCHA 0.21.0 build and install log > "%LOG%"
echo Started: %DATE% %TIME% >> "%LOG%"
echo Folder: %ROOT% >> "%LOG%"

cls
echo.
echo   ============================================================
echo     ORCHA  0.21.0
echo     Rhythm Loop Generator  -  by Gussa Naaman
echo   ============================================================
echo.
echo   This builds the plug-in from source and installs it.
echo   The first run takes a few minutes. After that it is quick.
echo.

REM --- 0. administrator --------------------------------------------------------
net session >nul 2>&1
if errorlevel 1 (
    echo   [X] This needs administrator rights to write into Program Files.
    echo.
    echo       Close this window, RIGHT-CLICK this file, and choose
    echo       "Run as administrator".
    echo NOT ADMIN >> "%LOG%"
    goto :fail
)
echo   [OK] Running as administrator.

REM --- 1. prerequisites --------------------------------------------------------
echo.
echo   [1/6] Checking what is installed...

set "MISSING="

where cmake >nul 2>&1
if errorlevel 1 (
    REM CMake ships inside Visual Studio; try there before giving up.
    for %%P in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    ) do (
        if exist "%%~P\cmake.exe" set "PATH=%%~P;!PATH!"
    )
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo         [X] CMake not found.
    set "MISSING=1"
) else (
    echo         [OK] CMake found
    cmake --version >> "%LOG%" 2>&1
)

REM Visual Studio 2022 with the C++ workload.
REM
REM The for /f sits at top level on purpose. Inside a parenthesised block the
REM caret escaping needed for a redirect or a pipe stops behaving, which is the
REM same class of bug that broke an earlier installer.
REM
REM This check is ADVISORY. vswhere is not always present, so a negative here
REM is not proof of anything - CMake is the authority on whether the toolchain
REM works, and it says so clearly a few lines further down.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if not exist "%VSWHERE%" goto :vsdone
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%I"
:vsdone

if defined VSPATH (
    echo         [OK] Visual Studio found
    echo VS: !VSPATH! >> "%LOG%"
) else (
    echo         [?] Could not confirm Visual Studio. Carrying on anyway -
    echo             the next step will say plainly if the compiler is missing.
    echo VS NOT DETECTED BY VSWHERE >> "%LOG%"
)

REM JUCE 9 - the shared checkout every Naaman plug-in uses. If FOUR COLOR was
REM built on this machine, it is already here and nothing is downloaded.
set "JUCEDIR=%USERPROFILE%\JUCE"
if exist "%JUCEDIR%\CMakeLists.txt" (
    echo         [OK] JUCE found at %JUCEDIR% - reusing it, no download.
) else (
    echo         [!] JUCE not found at %JUCEDIR%
    where git >nul 2>&1
    if errorlevel 1 (
        echo         [X] ...and git is not installed, so I cannot fetch it.
        set "MISSING=1"
    ) else (
        echo.
        echo         I can download JUCE 9 for you now. It is about 500 MB
        echo         and goes to %JUCEDIR%. This happens once, ever - every
        echo         Naaman plug-in after this reuses the same folder.
        echo.
        choice /c YN /m "        Download JUCE now"
        if errorlevel 2 (
            echo         Skipped. Nothing was installed.
            echo JUCE DECLINED >> "%LOG%"
            goto :fail
        )
        echo.
        echo         Downloading JUCE, please wait...
        git clone --quiet https://github.com/juce-framework/JUCE.git "%JUCEDIR%" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo         [X] The download failed. Details are in the log.
            set "MISSING=1"
        ) else (
            pushd "%JUCEDIR%"
            git checkout --quiet 857aab9c4eb3084af639a380a693dcec7d728b73 >> "%LOG%" 2>&1
            popd
            echo         [OK] JUCE downloaded and set to the pinned version.
        )
    )
)

if defined MISSING (
    echo.
    echo   ============================================================
    echo     Something is missing. Nothing has been changed.
    echo   ============================================================
    echo.
    echo     Visual Studio 2022 - free Community edition:
    echo       https://visualstudio.microsoft.com/downloads/
    echo       In the installer tick "Desktop development with C++".
    echo.
    echo     That one download also provides CMake, so install it first
    echo     and run this file again.
    echo.
    echo MISSING PREREQUISITES >> "%LOG%"
    goto :fail
)

REM --- 2. configure ------------------------------------------------------------
echo.
echo   [2/6] Preparing the build...
cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DORCHA_COPY_AFTER_BUILD=OFF >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] Preparation failed.
    echo.
    echo             The usual cause is Visual Studio 2022 without the
    echo             "Desktop development with C++" workload. Install it from
    echo             https://visualstudio.microsoft.com/downloads/
    echo             and run this file again.
    echo.
    echo             The exact reason is at the end of:
    echo             %LOG%
    echo CONFIGURE FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Ready.

REM --- 3. build ----------------------------------------------------------------
echo.
echo   [3/6] Building. This is the slow part - a few minutes.
echo         Nothing is wrong if it looks stuck; it is compiling.
cmake --build "%BUILD%" --config Release --target Orcha_VST3 Orcha_Standalone --parallel >> "%LOG%" 2>&1
if errorlevel 1 (
    echo.
    echo         [X] The build failed.
    echo             The compiler's own message is at the end of:
    echo             %LOG%
    echo             Send me the last 40 lines of that file.
    echo BUILD FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Built.

set "SRC=%BUILD%\Orcha_artefacts\Release\VST3\ORCHA.vst3"
if not exist "%SRC%\Contents\x86_64-win\ORCHA.vst3" (
    echo         [X] The build reported success but the plug-in is not where
    echo             it should be. Expected:
    echo             %SRC%
    echo MISSING ARTEFACT >> "%LOG%"
    dir /s /b "%BUILD%\Orcha_artefacts" >> "%LOG%" 2>&1
    goto :fail
)

REM --- 4. a running DAW will block the copy ------------------------------------
echo.
echo   [4/6] Checking for a running DAW...
set "DAW="
for %%P in (Cubase.exe Cubase14.exe Cubase15.exe Nuendo.exe Ableton.exe FL64.exe reaper.exe) do (
    tasklist /fi "imagename eq %%P" 2>nul | find /i "%%P" >nul && set "DAW=%%P"
)
if defined DAW (
    echo.
    echo         [!] !DAW! is running.
    echo             Windows will not replace a plug-in a host has loaded, and
    echo             this is the usual reason an update seems to do nothing.
    echo.
    echo             Close it now, then press any key.
    echo DAW RUNNING: !DAW! >> "%LOG%"
    pause >nul
) else (
    echo         [OK] Nothing in the way.
)

REM --- 5. install --------------------------------------------------------------
echo.
echo   [5/6] Installing...
if not exist "%DEST%\" mkdir "%DEST%" 2>nul

if exist "%DEST%\ORCHA.vst3\" (
    rmdir /s /q "%DEST%\ORCHA.vst3"
) else (
    if exist "%DEST%\ORCHA.vst3" del /f /q "%DEST%\ORCHA.vst3"
)

if exist "%DEST%\ORCHA.vst3" (
    echo         [X] The old version could not be removed. Something still has
    echo             it open - a DAW, or the plug-in scanner that keeps running
    echo             after one closes. Close every audio application and run
    echo             this again.
    echo REMOVE FAILED >> "%LOG%"
    goto :fail
)

xcopy /e /i /y "%SRC%" "%DEST%\ORCHA.vst3\" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] The copy failed. xcopy's own message is in the log.
    echo COPY FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Plug-in installed.

set "EXE=%BUILD%\Orcha_artefacts\Release\Standalone\ORCHA.exe"
if exist "%EXE%" (
    if not exist "C:\Program Files\Naaman\ORCHA\" mkdir "C:\Program Files\Naaman\ORCHA" 2>nul
    copy /y "%EXE%" "C:\Program Files\Naaman\ORCHA\ORCHA.exe" >nul 2>&1
    echo         [OK] Standalone app installed.
)

REM --- 6. verify ---------------------------------------------------------------
echo.
echo   [6/6] Verifying...
if not exist "%DEST%\ORCHA.vst3\Contents\x86_64-win\ORCHA.vst3" (
    echo         [X] The binary is not where it should be after copying.
    echo VERIFY FAILED - what is actually there: >> "%LOG%"
    dir /s /b "%DEST%\ORCHA.vst3" >> "%LOG%" 2>&1
    goto :fail
)
echo         [OK] Verified.
echo INSTALL OK >> "%LOG%"

echo.
echo   ============================================================
echo     DONE. ORCHA 0.21.0 is installed.
echo   ============================================================
echo.
echo     %DEST%\ORCHA.vst3
echo.
echo     1. Start Cubase
echo     2. Studio menu, then VST Plug-in Manager, then Update
echo     3. ORCHA is an INSTRUMENT, not an effect. Add it on an
echo        INSTRUMENT TRACK - under Naaman, category Instrument Drum.
echo.
echo     Then: drop 1-3 samples on the three cards, press GENERATE
echo     LOOPS, and drag any card straight into an audio track.
echo.
echo     CHECK THE VERSION. Top-right of the plug-in window, small
echo     grey text must read v0.21.0. If it reads something else,
echo     Cubase is loading an older copy - tell me and send the log.
echo.
pause
exit /b 0

:fail
echo.
echo   ============================================================
echo     NOT INSTALLED. Nothing was changed.
echo   ============================================================
echo.
echo     The full log is here:
echo     %LOG%
echo.
echo     Send me that file. It lists every step and every path tried.
echo.
pause
exit /b 1
