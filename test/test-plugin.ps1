# test-plugin.ps1 - Test clap-nr.clap with clap-validator
# Copies runtime DLLs to the build directory and runs validation

$ErrorActionPreference = "Stop"

$plugin = Join-Path $PSScriptRoot "..\build\Release\clap-nr.clap"
$pluginDir = Join-Path $PSScriptRoot "..\build\Release"
$libs = Join-Path $PSScriptRoot "..\libs"
$validator = "clap-validator.exe"

# Check if plugin exists
if (-not (Test-Path $plugin)) {
    Write-Host "ERROR: Plugin not found at $plugin" -ForegroundColor Red
    Write-Host "Run .\build-win.ps1 first." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Copying runtime DLLs to build\Release..." -ForegroundColor Cyan

# Files to copy
$files = @(
    @{ Src = "fftw\libfftw3-3.dll" },
    @{ Src = "fftw\libfftw3f-3.dll" },
    @{ Src = "rnnoise\rnnoise.dll" },
    @{ Src = "rnnoise\rnnoise_avx2.dll" },
    @{ Src = "rnnoise\rnnoise_weights_small.bin" },
    @{ Src = "rnnoise\rnnoise_weights_large.bin" },
    @{ Src = "specbleach\specbleach.dll" }
)

foreach ($file in $files) {
    $src = Join-Path $libs $file.Src
    $filename = Split-Path $src -Leaf
    
    if (Test-Path $src) {
        Copy-Item $src $pluginDir -Force
        Write-Host "  Copied: $filename" -ForegroundColor Gray
    }
    else {
        Write-Host "  WARNING: $filename not found at $src" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Running clap-validator on $plugin..." -ForegroundColor Cyan
Write-Host ""

# Run validator
try {
    & $validator validate $plugin
    $exitCode = $LASTEXITCODE
    
    Write-Host ""
    if ($exitCode -eq 0) {
        Write-Host "Done. Exit code: $exitCode" -ForegroundColor Green
    }
    else {
        Write-Host "Done. Exit code: $exitCode" -ForegroundColor Yellow
    }
}
catch {
    Write-Host ""
    Write-Host "ERROR: Failed to run clap-validator - $_" -ForegroundColor Red
    Write-Host "Make sure clap-validator.exe is in your PATH or in the test directory." -ForegroundColor Yellow
    $exitCode = 1
}

Write-Host ""
Read-Host "Press Enter to exit"
exit $exitCode
