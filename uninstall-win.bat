@echo off
:: uninstall.bat -- removes clap-nr.clap and its runtime DLLs from %CommonProgramFiles%\CLAP\
:: Run as Administrator.
setlocal

set DEST=%CommonProgramFiles%\CLAP

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Run as Administrator.
    pause
    exit /b 1
)

call :del "%DEST%\clap-nr.clap"        || exit /b %errorlevel%
call :del "%DEST%\libfftw3-3.dll"      || exit /b %errorlevel%
call :del "%DEST%\libfftw3f-3.dll"     || exit /b %errorlevel%
call :del "%DEST%\rnnoise.dll"         || exit /b %errorlevel%
call :del "%DEST%\rnnoise_avx2.dll"    || exit /b %errorlevel%
call :del "%DEST%\rnnoise_weights_small.bin" || exit /b %errorlevel%
call :del "%DEST%\rnnoise_weights_large.bin" || exit /b %errorlevel%
call :del "%DEST%\specbleach.dll"      || exit /b %errorlevel%

echo Uninstalled from %DEST%
pause
exit /b 0

:: -----------------------------------------------------------------------
:del
if not exist %1 exit /b 0
del /f /q %1 >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: failed to delete %~nx1 - if your CLAP host is open, close it and try again.
    pause
    exit /b 1
)
exit /b 0
