@echo off
setlocal

set PLUGIN=..\build\Release\clap-nr.clap
set PLUGINDIR=..\build\Release
set LIBS=..\libs
set VALIDATOR=clap-validator.exe

if not exist "%PLUGIN%" (
    echo ERROR: Plugin not found at %PLUGIN%
    echo Run build-win.bat first.
    pause
    exit /b 1
)

echo Copying runtime DLLs to build\Release ...
copy /Y "%LIBS%\fftw\libfftw3-3.dll"          "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\fftw\libfftw3f-3.dll"         "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\rnnoise\rnnoise.dll"           "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\rnnoise\rnnoise_avx2.dll"      "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\rnnoise\rnnoise_weights_small.bin" "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\rnnoise\rnnoise_weights_large.bin" "%PLUGINDIR%\" >nul
copy /Y "%LIBS%\specbleach\specbleach.dll"     "%PLUGINDIR%\" >nul

echo Running clap-validator on %PLUGIN%
echo.

%VALIDATOR% validate "%PLUGIN%"

echo.
echo Done. Exit code: %ERRORLEVEL%
pause
endlocal
