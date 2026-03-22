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
    pause
    exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%"

call :copy "%BUILD%"           clap-nr.clap    || goto :fail
call :copy "%LIBS%\fftw"      libfftw3-3.dll  || goto :fail
call :copy "%LIBS%\fftw"      libfftw3f-3.dll || goto :fail
call :copy "%LIBS%\rnnoise"   rnnoise.dll     || goto :fail
call :copy "%LIBS%\rnnoise"   rnnoise_avx2.dll || goto :fail
call :copy "%LIBS%\specbleach" specbleach.dll  || goto :fail

echo Plugin successfully installed to %DEST%, you can now test in your host app.
pause
exit /b 0

:fail
pause
exit /b 1

:: -----------------------------------------------------------------------
:copy
robocopy "%~1" "%DEST%" %2 /copy:dat /njh /njs /np >nul 2>&1
if %errorlevel% geq 8 (
    echo ERROR: failed to copy %2 -- if your CLAP host is open, close it and try again.
    exit /b 1
)
exit /b 0
