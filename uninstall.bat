@echo off
:: uninstall.bat -- removes clap-nr.clap and its runtime DLLs from %CommonProgramFiles%\CLAP\
:: Run as Administrator.
setlocal

set DEST=%CommonProgramFiles%\CLAP

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Run as Administrator.
    exit /b 1
)

call :del "%DEST%\clap-nr.clap"        || exit /b %errorlevel%
call :del "%DEST%\libfftw3-3.dll"      || exit /b %errorlevel%
call :del "%DEST%\libfftw3f-3.dll"     || exit /b %errorlevel%
call :del "%DEST%\rnnoise.dll"         || exit /b %errorlevel%
call :del "%DEST%\rnnoise_avx2.dll"    || exit /b %errorlevel%
call :del "%DEST%\specbleach.dll"      || exit /b %errorlevel%

echo Uninstalled from %DEST%
pause
exit /b 0

:: -----------------------------------------------------------------------
:del
if not exist %1 exit /b 0
del /f %1 >nul 2>&1
if %errorlevel% == 32 (
    echo ERROR: %~1 is locked. Close your CLAP host and run again.
    exit /b 32
)
if %errorlevel% neq 0 (
    echo ERROR: failed to delete %~1
    exit /b 1
)
exit /b 0
