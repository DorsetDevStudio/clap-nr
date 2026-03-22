@echo off
:: install.bat -- copies clap-nr.clap and runtime DLLs to %CommonProgramFiles%\CLAP\
:: Run as Administrator.
setlocal

set DEST=%CommonProgramFiles%\CLAP
set BUILD=%~dp0build\Release
set LIBS=%~dp0libs

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Run as Administrator.
    exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%"

call :copy "%BUILD%\clap-nr.clap"            "%DEST%\" || exit /b %errorlevel%
call :copy "%LIBS%\fftw\libfftw3-3.dll"      "%DEST%\" || exit /b %errorlevel%
call :copy "%LIBS%\fftw\libfftw3f-3.dll"     "%DEST%\" || exit /b %errorlevel%
call :copy "%LIBS%\rnnoise\rnnoise.dll"       "%DEST%\" || exit /b %errorlevel%
call :copy "%LIBS%\rnnoise\rnnoise_avx2.dll"  "%DEST%\" || exit /b %errorlevel%
call :copy "%LIBS%\specbleach\specbleach.dll"  "%DEST%\" || exit /b %errorlevel%

echo Plugin successfully installed to %DEST%, you can now test in your host app.
pause
exit /b 0

:: -----------------------------------------------------------------------
:copy
xcopy /y %1 %2 >nul 2>&1
if %errorlevel% == 32 (
    echo ERROR: %~1 is locked. Close your CLAP host and run again.
    exit /b 32
)
if %errorlevel% neq 0 (
    echo ERROR: failed to copy %~1
    exit /b 1
)
exit /b 0
