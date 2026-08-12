@echo off
setlocal enabledelayedexpansion
title ORCHA - Windows build

rem ---------------------------------------------------------------------------
rem  Builds ORCHA's VST3 + Standalone from source. Clones JUCE itself at a
rem  pinned commit, so there is nothing to configure.
rem
rem  Every step is checked. This script must never print "done" for something it
rem  did not do - a silent installer failure costs more than a loud one.
rem ---------------------------------------------------------------------------

set "JUCE_COMMIT=857aab9c4eb3084af639a380a693dcec7d728b73"
set "ROOT=%~dp0"

rem  This script ships BOTH at the bundle root and in packaging\. Run from
rem  packaging\ it would build into packaging\build, which is the wrong place.
rem  Walk up one level when the source is the parent's.
if not exist "%ROOT%CMakeLists.txt" (
  if exist "%ROOT%..\CMakeLists.txt" (
    pushd "%ROOT%.."
    set "ROOT=!CD!\"
    popd
  )
)

cd /d "%ROOT%"

echo.
echo   ORCHA - building the VST3 + Standalone
echo   --------------------------------------
echo   Folder: %ROOT%
echo.

rem --- is the source actually here? -------------------------------------------
rem  Double-clicking this .bat from inside Explorer's ZIP viewer extracts the
rem  script ALONE to a temp folder. Everything below would then fail with a
rem  confusing CMake error instead of the real reason.
if not exist "%ROOT%CMakeLists.txt" (
  echo   ERROR: ORCHA's source is not next to this script.
  echo.
  echo          You are probably running build-windows.bat straight out of
  echo          the ZIP. Windows extracts only the script that way.
  echo.
  echo          EXTRACT THE WHOLE ZIP to a real folder first, then run
  echo          build-windows.bat from inside that folder.
  goto :fail
)

rem --- the two tools we cannot do without ------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
  echo   ERROR: cmake is not on your PATH.
  echo          Install CMake and tick "Add CMake to the system PATH",
  echo          then open a NEW terminal and run this again.
  goto :fail
)

where git >nul 2>nul
if errorlevel 1 (
  echo   ERROR: git is not on your PATH.
  echo          Install Git for Windows, then open a NEW terminal.
  goto :fail
)

rem --- JUCE, at the exact commit this was tested against ----------------------
if exist "JUCE\CMakeLists.txt" (
  echo   JUCE is already here - leaving it alone.
) else (
  echo   Cloning JUCE ... this is the slow part, once.
  git clone --quiet https://github.com/juce-framework/JUCE.git JUCE
  if errorlevel 1 (
    echo   ERROR: could not clone JUCE. Check your internet connection.
    goto :fail
  )

  pushd JUCE
  git checkout --quiet %JUCE_COMMIT%
  if errorlevel 1 (
    popd
    echo   ERROR: could not check out the pinned JUCE commit %JUCE_COMMIT%.
    goto :fail
  )
  popd
)

if not exist "JUCE\CMakeLists.txt" (
  echo   ERROR: JUCE\CMakeLists.txt is missing after the clone.
  goto :fail
)

rem --- configure --------------------------------------------------------------
rem  ORCHA's CMake prefers %USERPROFILE%\JUCE when one exists (the shared
rem  checkout convention on the dev machine). This bundle was tested against
rem  the pinned .\JUCE, so force exactly that one.
echo.
echo   Configuring ...
cmake -B build -DCMAKE_BUILD_TYPE=Release -DORCHA_COPY_AFTER_BUILD=OFF ^
      "-DORCHA_JUCE_DIR_OVERRIDE=%ROOT%JUCE"
if errorlevel 1 (
  echo.
  echo   ERROR: cmake configure failed - see the message above.
  echo          "No CMAKE_CXX_COMPILER" means Visual Studio is missing the
  echo          "Desktop development with C++" workload.
  goto :fail
)

rem --- build ------------------------------------------------------------------
echo.
echo   Building ... 5-15 minutes the first time.
cmake --build build --config Release --target Orcha_VST3 Orcha_Standalone OrchaTests OrchaShot
if errorlevel 1 (
  echo.
  echo   ERROR: the build failed - see the message above.
  goto :fail
)

set "VST3=%ROOT%build\Orcha_artefacts\Release\VST3\ORCHA.vst3"
if not exist "%VST3%" (
  echo.
  echo   ERROR: the build reported success but %VST3% does not exist.
  goto :fail
)

echo.
echo   BUILT:  %VST3%
echo.

rem --- verify on THIS machine -------------------------------------------------
rem  The engine was verified on macOS. Nothing about that transfers to a
rem  different compiler and floating-point back end, so the same suite runs
rem  here and prints the same result - or this script stops.
set "TESTS=%ROOT%build\OrchaTests_artefacts\Release\OrchaTests.exe"
if not exist "%TESTS%" (
  echo   ERROR: the test suite was not built: %TESTS% is missing.
  goto :fail
)

echo   Running the engine suite ...
echo   ------------------------------------------------------------------
"%TESTS%"
if errorlevel 1 (
  echo   ------------------------------------------------------------------
  echo.
  echo   ERROR: the engine suite FAILED on this machine.
  echo          The failing lines above say which check broke.
  echo          Do not install this build - send a screenshot instead.
  goto :fail
)
echo   ------------------------------------------------------------------

rem  OrchaShot with no samples builds the real editor once and saves a PNG -
rem  a cheap proof that the UI constructs and paints on this machine.
set "SHOT=%ROOT%build\OrchaShot_artefacts\Release\OrchaShot.exe"
if not exist "%SHOT%" (
  echo   ERROR: the UI smoke tool was not built: %SHOT% is missing.
  goto :fail
)

echo   Running the UI smoke check ...
"%SHOT%" "%ROOT%ui-shots"
if errorlevel 1 (
  echo.
  echo   ERROR: the UI smoke check FAILED on this machine.
  echo          Do not install this build - send a screenshot instead.
  goto :fail
)
if not exist "%ROOT%ui-shots\01-empty.png" (
  echo.
  echo   ERROR: the UI smoke check did not produce ui-shots\01-empty.png.
  goto :fail
)
echo   Every check passed on this machine.
echo.

rem --- optional install -------------------------------------------------------
set "TARGET=%CommonProgramFiles%\VST3"
set "DEST=%TARGET%\ORCHA.vst3"
echo   Copy it to %TARGET% ? That is where Cubase looks.
set /p ANSWER=  [y/N]

if /i not "%ANSWER%"=="y" (
  echo.
  echo   Left in the build folder. Point Cubase at it, or copy it yourself.
  goto :done
)

rem  A running DAW holds the plug-in open and Windows refuses to replace it -
rem  silently, from this script's point of view. Check first and say so.
set "DAWRUNNING="
rem  Quoted, and dereferenced with %%~P: a bare `for` list splits on spaces,
rem  so "Studio One.exe" would have become two bogus process names.
for %%P in ("Cubase.exe" "Nuendo.exe" "Ableton Live.exe" "reaper.exe" "FL64.exe" "Studio One.exe" "Bitwig Studio.exe") do (
  tasklist /FI "IMAGENAME eq %%~P" 2>nul | find /I "%%~P" >nul && set "DAWRUNNING=%%~P"
)
if defined DAWRUNNING (
  echo.
  echo   ERROR: %DAWRUNNING% is running. Windows will not let this script
  echo          replace a plug-in a DAW has open, and the failure is silent.
  echo          Close it and run this again.
  goto :fail
)

rem  An old install can be a single FILE rather than a folder bundle. rmdir
rem  cannot delete a file, xcopy cannot create a folder whose name a file has
rem  taken, and BOTH fail quietly. Handle both shapes, then VERIFY.
if exist "%DEST%\" (
  echo   Removing the previous folder install ...
  rmdir /S /Q "%DEST%"
) else (
  if exist "%DEST%" (
    echo   Removing the previous FILE install ...
    del /F /Q "%DEST%"
  )
)

if exist "%DEST%" (
  echo.
  echo   ERROR: the previous install at
  echo       %DEST%
  echo   could not be removed. This is almost always administrator rights or
  echo   a DAW still holding it. Nothing has been changed.
  goto :fail
)

echo   Copying ...
rem  No >nul here: if xcopy has something to say, the user needs to read it.
xcopy /E /I /Y "%VST3%" "%DEST%"
if errorlevel 1 (
  echo.
  echo   COPY FAILED - almost always because this needs administrator rights.
  echo   Right-click build-windows.bat and choose "Run as administrator",
  echo   or copy this folder by hand:
  echo       from  %VST3%
  echo       to    %DEST%
  goto :fail
)

rem  Verify the PAYLOAD, not just the folder: an empty ORCHA.vst3 directory
rem  counts as "exists" and Cubase would scan it and fail.
if not exist "%DEST%\Contents\x86_64-win\ORCHA.vst3" (
  echo.
  echo   COPY FAILED: the bundle arrived incomplete.
  echo       expected %DEST%\Contents\x86_64-win\ORCHA.vst3
  dir "%DEST%" /S /B
  goto :fail
)

echo   Installed to %DEST%
echo   (verified: Contents\x86_64-win\ORCHA.vst3 is on disk)
echo.
echo   In Cubase: Studio ^> VST Plug-in Manager ^> rescan.
echo   ORCHA is an INSTRUMENT - add it on an Instrument track, not as an
echo   insert. It is filed under Instrument / Drum.
echo.
echo   The standalone app is also ready, no install needed:
echo       %ROOT%build\Orcha_artefacts\Release\Standalone\ORCHA.exe

:done
echo.
echo   Finished.
pause
exit /b 0

:fail
echo.
echo   STOPPED. Nothing was installed.
pause
exit /b 1
