# install-win.ps1 - Install clap-nr.clap and runtime DLLs to %CommonProgramFiles%\CLAP\
# Automatically requests elevation if not running as Administrator.

# -- Auto-elevate if not running as Administrator ------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Requesting administrator privileges..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# -- Installation --------------------------------------------------------------
$ErrorActionPreference = "Stop"

$DEST = "$env:CommonProgramFiles\CLAP"
$BUILD = Join-Path $PSScriptRoot "build\Release"
$LIBS = Join-Path $PSScriptRoot "libs"

Write-Host "Installing clap-nr to $DEST..." -ForegroundColor Cyan

# Create destination directory if it doesn't exist
if (-not (Test-Path $DEST)) {
    New-Item -ItemType Directory -Path $DEST -Force | Out-Null
}

# Copy plugin
$pluginSrc = Join-Path $BUILD "clap-nr.clap"
if (-not (Test-Path $pluginSrc)) {
    Write-Host "ERROR: clap-nr.clap not found at $pluginSrc" -ForegroundColor Red
    Write-Host "Build the plugin first with: .\build-win.ps1" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}
Copy-Item $pluginSrc $DEST -Force

# Copy runtime DLLs
$files = @(
    @{ Path = "fftw\libfftw3-3.dll" },
    @{ Path = "fftw\libfftw3f-3.dll" },
    @{ Path = "rnnoise\rnnoise.dll" },
    @{ Path = "rnnoise\rnnoise_avx2.dll" },
    @{ Path = "rnnoise\rnnoise_weights_small.bin" },
    @{ Path = "rnnoise\rnnoise_weights_large.bin" },
    @{ Path = "specbleach\specbleach.dll" }
)

foreach ($file in $files) {
    $src = Join-Path $LIBS $file.Path
    $filename = Split-Path $src -Leaf
    
    if (-not (Test-Path $src)) {
        Write-Host "WARNING: $filename not found at $src" -ForegroundColor Yellow
        continue
    }
    
    try {
        Copy-Item $src $DEST -Force
    }
    catch {
        Write-Host "ERROR: Failed to copy $filename" -ForegroundColor Red
        Write-Host "If your CLAP host is open, close it and try again." -ForegroundColor Yellow
        Read-Host "Press Enter to exit"
        exit 1
    }
}

Write-Host ""
Write-Host "SUCCESS: Plugin installed to $DEST" -ForegroundColor Green
Write-Host "You can now load the plugin in your CLAP host." -ForegroundColor Green
Write-Host ""
Read-Host "Press Enter to exit"
