@echo off
setlocal

:: ---------------------------------------------------------------
:: install.bat  --  copy clap-nr.clap + runtime DLLs to the system
::                  CLAP plugin folder so any CLAP host picks it up.
::
:: Standard location:  %CommonProgramFiles%\CLAP\
::   (expands to C:\Program Files\Common Files\CLAP\ on most systems)
::
:: Requires elevation because the target is under Program Files.
:: ---------------------------------------------------------------

set DEST=%CommonProgramFiles%\CLAP
set BUILD=%~dp0build\Release
set LIBS=%~dp0libs

echo.
echo  Installing clap-nr to: %DEST%
echo.

:: --- elevation check ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo  ERROR: This script must be run as Administrator.
    echo  Right-click install.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

:: --- create destination if it doesn't exist ---
if not exist "%DEST%" (
    echo  Creating %DEST% ...
    mkdir "%DEST%"
    if errorlevel 1 goto fail
)

:: --- copy the plugin itself ---
echo  Copying clap-nr.clap ...
copy /y "%BUILD%\clap-nr.clap" "%DEST%\" >nul
if errorlevel 1 goto fail

:: --- copy runtime DLLs (load from same directory as the .clap) ---
echo  Copying runtime DLLs ...
copy /y "%LIBS%\fftw\libfftw3-3.dll"      "%DEST%\" >nul
if errorlevel 1 goto fail
copy /y "%LIBS%\fftw\libfftw3f-3.dll"     "%DEST%\" >nul
if errorlevel 1 goto fail
copy /y "%LIBS%\rnnoise\rnnoise.dll"       "%DEST%\" >nul
if errorlevel 1 goto fail
copy /y "%LIBS%\rnnoise\rnnoise_avx2.dll"  "%DEST%\" >nul
if errorlevel 1 goto fail
copy /y "%LIBS%\specbleach\specbleach.dll" "%DEST%\" >nul
if errorlevel 1 goto fail

echo.
echo  Done.  Restart your CLAP host to pick up the new build.
echo.
pause
exit /b 0

:fail
echo.
echo  ERROR: copy failed.  Check that the build succeeded and that
echo         all DLLs are present in the libs\ subdirectories.
echo.
pause
exit /b 1
