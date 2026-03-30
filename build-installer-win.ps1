# build-installer-win.ps1 - Build clap-nr Windows installer with optional code signing
#
# NOTE: We are signing the Windows installer and files with Stuart G5STU's code signing key
# which is issued to "Station Master Group Ltd" - this will prevent security warning when
# installing on Windows. Code signing is a VERY expensive and involved process to setup.
# Only Stuart can sign files using a physical code signing key, so he is the only one that
# can publish new releases.
#
# Usage:
#   .\build-installer-win.ps1            (interactive - prompts for signing)
#   .\build-installer-win.ps1 -NoSign    (skip all signing, for test builds)
#
# Requirements:
#   - Inno Setup 7  (C:\Program Files\Inno Setup 7\ISCC.exe)
#   - Windows SDK signtool.exe  (C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64)
#   - SafeNet Authentication Client + EV token inserted (for signing)

param(
    [switch]$NoSign
)

$ErrorActionPreference = "Stop"

# -----------------------------------------------------------------------
# Read version from src/version.h - single source of truth
# -----------------------------------------------------------------------
$versionHeader = Get-Content (Join-Path $PSScriptRoot "src\version.h") -Raw
if ($versionHeader -match '#define\s+CLAP_NR_VERSION_MAJOR\s+(\d+)') { $verMajor = $matches[1] }
if ($versionHeader -match '#define\s+CLAP_NR_VERSION_MINOR\s+(\d+)') { $verMinor = $matches[1] }
if ($versionHeader -match '#define\s+CLAP_NR_VERSION_PATCH\s+(\d+)') { $verPatch = $matches[1] }
$version = "$verMajor.$verMinor.$verPatch"

$script = Join-Path $PSScriptRoot "installer.iss"
$buildDir = Join-Path $PSScriptRoot "build"
$outputDir = Join-Path $PSScriptRoot "dist"
$installerExe = Join-Path $outputDir "clap-nr-$version-setup.exe"
$pluginExe = Join-Path $buildDir "Release\clap-nr.clap"

Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host " clap-nr $version installer build" -ForegroundColor Cyan
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host ""

# -----------------------------------------------------------------------
# 1. Locate Inno Setup compiler (ISCC.exe)
# -----------------------------------------------------------------------
$iscc = "C:\Program Files\Inno Setup 7\ISCC.exe"
if (-not (Test-Path $iscc)) {
    Write-Host "ERROR: Inno Setup 7 not found at: $iscc" -ForegroundColor Red
    Write-Host "Download from: https://jrsoftware.org/isdl.php" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}
Write-Host "Inno Setup:  $iscc" -ForegroundColor Gray

# -----------------------------------------------------------------------
# 2. Locate signtool.exe (Windows SDK)
# -----------------------------------------------------------------------
$signtool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
if (-not (Test-Path $signtool)) {
    Write-Host "ERROR: signtool.exe not found." -ForegroundColor Red
    Write-Host "Expected: $signtool" -ForegroundColor Yellow
    Write-Host "Install the Windows 10 SDK (version 10.0.22621.0) and re-run." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}
Write-Host "signtool:    $signtool" -ForegroundColor Gray

# -----------------------------------------------------------------------
# 3. Ask whether to sign
# -----------------------------------------------------------------------
$doSign = $false
$timestampUrl = "http://timestamp.digicert.com"
$certName = "STATION MASTER GROUP LTD"

if ($NoSign) {
    Write-Host "Signing:     skipped (-NoSign)" -ForegroundColor Yellow
}
else {
    Write-Host ""
    $response = Read-Host "Sign with SafeNet EV certificate ($certName)? [Y/N]"
    if ($response -eq 'Y' -or $response -eq 'y') {
        $doSign = $true
        Write-Host ""
        Write-Host "Make sure your SafeNet USB token is inserted now." -ForegroundColor Yellow
        Write-Host "The SafeNet PIN dialog will appear each time a file is signed." -ForegroundColor Yellow
    }
}

# -----------------------------------------------------------------------
# 4. Build
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host " Building clap-nr..." -ForegroundColor Cyan
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan

try {
    cmake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Build failed." -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
    Write-Host "Build succeeded." -ForegroundColor Green
}
catch {
    Write-Host ""
    Write-Host "ERROR: Build failed - $_" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# -----------------------------------------------------------------------
# 5. Sign clap-nr.clap (before bundling it into the installer)
# -----------------------------------------------------------------------
if ($doSign) {
    Write-Host ""
    Write-Host "Signing clap-nr.clap..." -ForegroundColor Cyan
    Write-Host "(SafeNet PIN dialog may appear)" -ForegroundColor Yellow
    
    & $signtool sign `
        /n $certName `
        /fd sha256 `
        /td sha256 `
        /tr $timestampUrl `
        /a `
        /sm `
        $pluginExe
        
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Signing clap-nr.clap failed." -ForegroundColor Red
        Write-Host " - Check that your USB token is inserted and the PIN is correct." -ForegroundColor Yellow
        Read-Host "Press Enter to exit"
        exit 1
    }
    Write-Host "Signed:  clap-nr.clap" -ForegroundColor Green
}

# -----------------------------------------------------------------------
# 6. Compile the Inno Setup installer
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host " Compiling installer..." -ForegroundColor Cyan
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan

if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

try {
    & $iscc "/DAppVersion=$version" "/O$outputDir" "/Fclap-nr-$version-setup" $script
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Inno Setup compilation failed." -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
    Write-Host "Installer: $installerExe" -ForegroundColor Green
}
catch {
    Write-Host ""
    Write-Host "ERROR: Inno Setup compilation failed - $_" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# -----------------------------------------------------------------------
# 7. Sign the installer .exe
# -----------------------------------------------------------------------
if ($doSign) {
    Write-Host ""
    Write-Host "Signing installer .exe..." -ForegroundColor Cyan
    Write-Host "(SafeNet PIN dialog may appear)" -ForegroundColor Yellow
    
    & $signtool sign `
        /n $certName `
        /fd sha256 `
        /td sha256 `
        /tr $timestampUrl `
        /a `
        /sm `
        $installerExe
        
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Signing the installer failed." -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
    Write-Host "Signed:  $installerExe" -ForegroundColor Green
}

# -----------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host " Done." -ForegroundColor Green
Write-Host " Output: $installerExe" -ForegroundColor Green
Write-Host "-----------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host ""
Read-Host "Press Enter to exit"
