@echo off

:: NOTE: we are signing the Windows installer and files with Stuart G5STU's code signing key 
:: which is issued to "Station Master Group Ltd" - this will prevent security warning when 
:: installing on Windows. Code signing is a VERY expensive and involved process to setup.
:: Only Stuart can sign files using a physical code signing key, so he is the onyl one that 
:: can publish new releases.

:: -----------------------------------------------------------------------
:: build-installer.bat
::
:: Builds clap-nr, optionally signs the plugin with your SafeNet EV
:: certificate, compiles the Inno Setup installer, then optionally signs
:: the resulting installer .exe.
::
:: Usage:
::   build-installer.bat            (interactive - prompts for signing)
::   build-installer.bat /nosign    (skip all signing, for test builds)
::
:: Requirements:
::   - Inno Setup 7  (C:\Program Files\Inno Setup 7\ISCC.exe)
::   - Windows SDK signtool.exe  (C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64)
::   - SafeNet Authentication Client + EV token inserted (for signing)
:: -----------------------------------------------------------------------
setlocal enabledelayedexpansion

set "PF86=%ProgramFiles(x86)%"

:: Read version from src/version.h - single source of truth
for /f "tokens=3" %%a in ('findstr /c:"#define CLAP_NR_VERSION_MAJOR" "%~dp0src\version.h"') do set VER_MAJOR=%%a
for /f "tokens=3" %%a in ('findstr /c:"#define CLAP_NR_VERSION_MINOR" "%~dp0src\version.h"') do set VER_MINOR=%%a
for /f "tokens=3" %%a in ('findstr /c:"#define CLAP_NR_VERSION_PATCH" "%~dp0src\version.h"') do set VER_PATCH=%%a
set VERSION=%VER_MAJOR%.%VER_MINOR%.%VER_PATCH%
set SCRIPT=%~dp0installer.iss
set BUILD_DIR=%~dp0build
set OUTPUT_DIR=%~dp0dist
set INSTALLER_EXE=%OUTPUT_DIR%\clap-nr-%VERSION%-setup.exe
set PLUGIN_EXE=%BUILD_DIR%\Release\clap-nr.clap

:: Parse /nosign switch
set FORCE_NOSIGN=0
if /i "%~1"=="/nosign" set FORCE_NOSIGN=1

echo -----------------------------------------------------------------------
echo  clap-nr %VERSION% installer build
echo -----------------------------------------------------------------------

:: -----------------------------------------------------------------------
:: 1. Locate Inno Setup compiler (ISCC.exe)
:: -----------------------------------------------------------------------
set "ISCC=C:\Program Files\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" (
    echo.
    echo ERROR: Inno Setup 7 not found at: %ISCC%
    echo Download from:  https://jrsoftware.org/isdl.php
    pause & exit /b 1
)
echo Inno Setup:  %ISCC%

:: -----------------------------------------------------------------------
:: 2. Locate signtool.exe  (Windows SDK)
:: -----------------------------------------------------------------------
:: Build path using !PF86! so no literal (x86) parens appear in the script source.
set "SIGNTOOL=!PF86!\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
if not exist "!SIGNTOOL!" (
    echo.
    echo ERROR: signtool.exe not found.
    echo Expected: !SIGNTOOL!
    echo Install the Windows 10 SDK ^(version 10.0.22621.0^) and re-run.
    pause & exit /b 1
)
echo signtool:    !SIGNTOOL!

:: -----------------------------------------------------------------------
:: 3. Ask whether to sign
:: -----------------------------------------------------------------------
set DO_SIGN=N
set TIMESTAMP_URL=http://timestamp.digicert.com
set CERT_NAME=STATION MASTER GROUP LTD

if %FORCE_NOSIGN%==1 (
    echo Signing:     skipped  ^(/nosign^)
    goto :build
)

echo.
set /p DO_SIGN="Sign with SafeNet EV certificate ^(%CERT_NAME%^)? [Y/N]: "
if /i not "%DO_SIGN%"=="Y" goto :build

echo.
echo Make sure your SafeNet USB token is inserted now.
echo The SafeNet PIN dialog will appear each time a file is signed.

:: -----------------------------------------------------------------------
:: 4. Build
:: -----------------------------------------------------------------------
:build
echo.
echo -----------------------------------------------------------------------
echo  Building clap-nr...
echo -----------------------------------------------------------------------
cmake --build "%BUILD_DIR%" --config Release
if %errorlevel% neq 0 (
    echo.
    echo ERROR: build failed.
    pause & exit /b 1
)
echo Build succeeded.

:: -----------------------------------------------------------------------
:: 5. Sign clap-nr.clap (before bundling it into the installer)
:: -----------------------------------------------------------------------
if /i not "%DO_SIGN%"=="Y" goto :compile_installer

echo.
echo Signing clap-nr.clap...
echo ^(SafeNet PIN dialog may appear^)
"%SIGNTOOL%" sign ^
    /n "%CERT_NAME%" ^
    /fd sha256 ^
    /td sha256 ^
    /tr "%TIMESTAMP_URL%" ^
    /a ^
    /sm ^
    "%PLUGIN_EXE%"
if %errorlevel% neq 0 (
    echo.
    echo ERROR: signing clap-nr.clap failed.
    echo  - Check that your USB token is inserted and the PIN is correct.
    pause & exit /b 1
)
echo Signed:  clap-nr.clap

:: -----------------------------------------------------------------------
:: 6. Compile the Inno Setup installer
:: -----------------------------------------------------------------------
:compile_installer
echo.
echo -----------------------------------------------------------------------
echo  Compiling installer...
echo -----------------------------------------------------------------------
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

"%ISCC%" /DAppVersion=%VERSION% /O"%OUTPUT_DIR%" /F"clap-nr-%VERSION%-setup" "%SCRIPT%"
if %errorlevel% neq 0 (
    echo.
    echo ERROR: Inno Setup compilation failed.
    pause & exit /b 1
)
echo Installer: %INSTALLER_EXE%

:: -----------------------------------------------------------------------
:: 7. Sign the installer .exe
:: -----------------------------------------------------------------------
if /i not "%DO_SIGN%"=="Y" goto :done

echo.
echo Signing installer .exe...
echo ^(SafeNet PIN dialog may appear^)
"%SIGNTOOL%" sign ^
    /n "%CERT_NAME%" ^
    /fd sha256 ^
    /td sha256 ^
    /tr "%TIMESTAMP_URL%" ^
    /a ^
    /sm ^
    "%INSTALLER_EXE%"
if %errorlevel% neq 0 (
    echo.
    echo ERROR: signing the installer failed.
    pause & exit /b 1
)
echo Signed:  %INSTALLER_EXE%

:: -----------------------------------------------------------------------
:: Done
:: -----------------------------------------------------------------------
:done
echo.
echo -----------------------------------------------------------------------
echo  Done.
echo  Output: %INSTALLER_EXE%
echo -----------------------------------------------------------------------
pause
exit /b 0
